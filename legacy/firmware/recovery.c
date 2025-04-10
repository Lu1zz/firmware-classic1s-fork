/*
 * This file is part of the Trezor project, https://trezor.io/
 *
 * Copyright (C) 2014 Pavol Rusnak <stick@satoshilabs.com>
 * Copyright (C) 2016 Jochen Hoenicke <hoenicke@gmail.com>
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "recovery.h"
#include <ctype.h>
#include <stdint.h>
#include "bip39.h"
#include "buttons.h"
#include "config.h"
#include "fsm.h"
#include "gettext.h"
#include "layout2.h"
#include "memzero.h"
#include "messages.h"
#include "messages.pb.h"
#include "oled.h"
#include "protect.h"
#include "recovery-table.h"
#include "rng.h"
#include "se_chip.h"
#include "timer.h"
#include "usb.h"
#include "util.h"

/* number of words expected in the new seed */
static uint32_t word_count;

/* recovery mode */
static enum {
  RECOVERY_NONE = 0,       // recovery not in progress
  RECOVERY_SCRAMBLED = 1,  // standard recovery by scrambled plain text words
  RECOVERY_MATRIX = 2,     // advanced recovery by matrix entry
} recovery_mode = RECOVERY_NONE;

/* True if we should not write anything back to config
 * (can be used for testing seed for correctness).
 */
static bool dry_run;

/* True if we should check that seed corresponds to bip39.
 */
static bool enforce_wordlist;

/* For scrambled recovery Trezor may ask for faked words if
 * seed is short.  This contains the fake word.
 */
static char fake_word[12];

/* Word position in the seed that we are currently asking for.
 * This is 0 if we ask for a fake word.  Only for scrambled recovery.
 */
static uint32_t word_pos;

/* Scrambled recovery:  How many words has the user already entered.
 * Matrix recovery: How many digits has the user already entered.
 */
static uint32_t word_index;

/* Order in which we ask for the words.  It holds that
 * word_order[word_index] == word_pos.  Only for scrambled recovery.
 */
static char word_order[24];

/* The recovered seed.  This is filled during the recovery process.
 */
static char words[24][12];

/* The "pincode" of the current word.  This is basically the "pin"
 * that the user would have entered for the current word if the words
 * were displayed in alphabetical order.  Note that it is base 9, not
 * base 10.  Only for matrix recovery.
 */
static uint16_t word_pincode;

/* The pinmatrix currently displayed on screen.
 * Only for matrix recovery.
 */
static uint8_t word_matrix[9];

static bool recovery_byself = false;

/* The words are stored in two tables.
 *
 * The low bits of the first table (TABLE1) store the index into the
 * second table, for each of the 81 choices for the first two levels
 * of the matrix.  The final entry points to the final entry of the
 * second table.  The difference TABLE1(idx+1)-TABLE1(idx) gives the
 * number of choices for the third level.  The value
 * TABLE2(TABLE1(idx)) gives the index of the first word in the range
 * and TABLE2(TABLE1(idx+1))-1 gives the index of the last word.
 *
 * The low bits of the second table (TABLE2) store the index into the
 * word list for each of the choices for the first three levels.  The
 * final entry stores the value 2048 (number of bip39 words).  table.
 * The difference TABLE2(idx+1)-TABLE2(idx) gives the number of
 * choices for the last level.  The value TABLE2(idx) gives the index
 * of the first word in the range and TABLE2(idx)-1 gives the index of
 * the last word.
 *
 * The high bits in each table is the "prefix length", i.e. the number
 * of significant letters for the corresponding choice.  There is no
 * prefix length or table for the very first level, as the prefix length
 * is always one and there are always nine choices on the second level.
 */
#define MASK_IDX(x) ((x)&0xfff)
#define TABLE1(x) MASK_IDX(word_table1[x])
#define TABLE2(x) MASK_IDX(word_table2[x])

#define goto_check(label)       \
  if (layoutLast == layoutHome) \
    return false;               \
  else                          \
    goto label;

/* Helper function to format a two digit number.
 * Parameter dest is buffer containing the string. It should already
 * start with "##th".  The number is written in place.
 * Parameter number gives the number that we should format.
 */
static void format_number(char *dest, int number) {
  strcat(dest, "##th");
  if (dest[0] == '#') {
    if (number < 10) {
      dest[0] = ' ';
    } else {
      dest[0] = '0' + number / 10;
    }
    dest[1] = '0' + number % 10;
    if (number == 1 || number == 21) {
      dest[2] = 's';
      dest[3] = 't';
    } else if (number == 2 || number == 22) {
      dest[2] = 'n';
      dest[3] = 'd';
    } else if (number == 3 || number == 23) {
      dest[2] = 'r';
      dest[3] = 'd';
    }
  } else {
    if (number < 10) {
      dest[3] = ' ';
    } else {
      dest[3] = '0' + number / 10;
    }
    dest[4] = '0' + number % 10;
  }
  strcat(dest, " ");
  strcat(dest, __("word"));
}

/* Send a request for a new word/matrix code to the PC.
 */
static void recovery_request(void) {
  WordRequest resp = {0};
  memzero(&resp, sizeof(WordRequest));
  if (recovery_mode == RECOVERY_SCRAMBLED) {
    resp.type = WordRequestType_WordRequestType_Plain;
  } else if (word_index % 4 == 3) {
    resp.type = WordRequestType_WordRequestType_Matrix6;
  } else {
    resp.type = WordRequestType_WordRequestType_Matrix9;
  }
  msg_write(MessageType_MessageType_WordRequest, &resp);
}

extern bool generate_seed_steps(void);

/* Called when the last word was entered.
 * Check mnemonic and send success/failure.
 */
static bool recovery_done(bool gohome) {
  bool success = false;
  uint8_t key;
  char new_mnemonic[MAX_MNEMONIC_LEN + 1] = {0};

  strlcpy(new_mnemonic, words[0], sizeof(new_mnemonic));
  for (uint32_t i = 1; i < word_count; i++) {
    strlcat(new_mnemonic, " ", sizeof(new_mnemonic));
    strlcat(new_mnemonic, words[i], sizeof(new_mnemonic));
  }

  if (!enforce_wordlist || mnemonic_check(new_mnemonic)) {
    // New mnemonic is valid.
    if (!dry_run) {
      // Update mnemonic on config.
      if (config_setMnemonic(new_mnemonic, true)) {
        if (!enforce_wordlist) {
          // not enforcing => mark config as imported
          config_setImported(true);
        }
        if (!recovery_byself) {
          fsm_sendSuccess("Device recovered");
        } else {
        }
        success = true;
        layoutSwipe();
      } else {
        fsm_sendFailure(FailureType_Failure_ProcessError,
                        "Failed to store mnemonic");
      }
      memzero(new_mnemonic, sizeof(new_mnemonic));
    } else {
      bool match =
          (config_isInitialized() && config_containsMnemonic(new_mnemonic));
      memzero(new_mnemonic, sizeof(new_mnemonic));
      if (recovery_byself) {
        if (match) {
          layoutDialogCenterAdapterV2(
              NULL, &bmp_icon_ok, NULL, &bmp_bottom_right_arrow, NULL, NULL,
              NULL, NULL, NULL, NULL,
              _(C__RECOVERY_PHRASE_MATCHED_YOUR_BACKUP_IS_CORRECT));
          while (1) {
            key = protectWaitKey(0, 1);
            if (key == KEY_CONFIRM) {
              break;
            }
          }
        } else {
          layoutDialogCenterAdapterV2(
              NULL, &bmp_icon_error, NULL, &bmp_bottom_right_arrow, NULL, NULL,
              NULL, NULL, NULL, NULL,
              _(C__RECOVERY_PHRASE_IS_VALID_BUT_DOES_NOT_MATCH_CHECK_AND_TRY_AGAIN));
          while (1) {
            key = protectWaitKey(0, 1);
            if (key == KEY_CONFIRM) {
              break;
            }
          }
        }
      } else {
        if (match) {
          layoutDialogCenterAdapterV2(
              NULL, &bmp_icon_ok, NULL, &bmp_bottom_right_arrow, NULL, NULL,
              NULL, NULL, NULL, NULL,
              _(C__RECOVERY_PHRASE_MATCHED_YOUR_BACKUP_IS_CORRECT));
          protectButton(ButtonRequestType_ButtonRequest_Other, true);
          fsm_sendSuccess(
              "The seed is valid and matches the one in the device");
        } else {
          layoutDialogCenterAdapterV2(
              NULL, &bmp_icon_error, NULL, &bmp_bottom_right_arrow, NULL, NULL,
              NULL, NULL, NULL, NULL,
              _(C__RECOVERY_PHRASE_IS_VALID_BUT_DOES_NOT_MATCH_CHECK_AND_TRY_AGAIN));
          protectButton(ButtonRequestType_ButtonRequest_Other, true);
          fsm_sendFailure(
              FailureType_Failure_DataError,
              "The seed is valid but does not match the one in the device");
        }
      }
    }
  } else {
    // New mnemonic is invalid.
    memzero(new_mnemonic, sizeof(new_mnemonic));
    if (recovery_byself) {
      layoutDialogCenterAdapterV2(
          NULL, &bmp_icon_error, NULL, &bmp_bottom_right_retry, NULL, NULL,
          NULL, NULL, NULL, NULL,
          _(C__INVALID_RECOVERY_PHRASE_EXCLAM_CHECK_AND_TRY_AGAIN));
      while (1) {
        key = protectWaitKey(0, 1);
        if (key == KEY_CONFIRM) {
          break;
        }
      }
    } else {
      if (!dry_run) {
        session_clear(true);
      } else {
        layoutDialogCenterAdapterV2(
            NULL, &bmp_icon_error, NULL, &bmp_bottom_right_retry, NULL, NULL,
            NULL, NULL, NULL, NULL,
            _(C__INVALID_RECOVERY_PHRASE_EXCLAM_CHECK_AND_TRY_AGAIN));
        protectButton(ButtonRequestType_ButtonRequest_Other, true);
      }
      fsm_sendFailure(FailureType_Failure_DataError,
                      "Invalid seed, are words in correct order?");
    }
  }
  memzero(words, sizeof(words));
  word_pincode = 0;
  recovery_mode = RECOVERY_NONE;
  if (gohome) {
    layoutHome();
  }

  return success;
}

/* Helper function for matrix recovery:
 * Formats a string describing the word range from first to last where
 * prefixlen gives the number of characters in first and last that are
 * significant, i.e. the word before first or the word after last differ
 * exactly at the prefixlen-th character.
 *
 * Invariants:
 *  memcmp("first - 1", first, prefixlen) != 0
 *  memcmp(last, "last + 1", prefixlen) != 0
 *  first[prefixlen-2] == last[prefixlen-2]  except for range WI-Z.
 */
static void add_choice(char choice[12], int prefixlen, const char *first,
                       const char *last) {
  // assert 1 <= prefixlen <= 4
  char *dest = choice;
  for (int i = 0; i < prefixlen; i++) {
    *dest++ = toupper((int)first[i]);
  }
  if (first[0] != last[0]) {
    /* special case WI-Z; also used for T-Z, etc. */
    *dest++ = '-';
    *dest++ = toupper((int)last[0]);
  } else if (last[prefixlen - 1] == first[prefixlen - 1]) {
    /* single prefix */
  } else if (prefixlen < 3) {
    /* AB-AC, etc. */
    *dest++ = '-';
    for (int i = 0; i < prefixlen; i++) {
      *dest++ = toupper((int)last[i]);
    }
  } else {
    /* RE[A-M] etc. */
    /* remove last and replace with space */
    dest[-1] = ' ';
    if (first[prefixlen - 1]) {
      /* handle special case: CAN[-D] */
      *dest++ = toupper((int)first[prefixlen - 1]);
    }
    *dest++ = '-';
    *dest++ = toupper((int)last[prefixlen - 1]);
  }
  *dest++ = 0;
}

/* Helper function for matrix recovery:
 * Display the recovery matrix given in choices.  If twoColumn is set
 * use 2x3 layout, otherwise 3x3 layout.  Also generates a random
 * scrambling and stores it in word_matrix.
 */
static void display_choices(bool twoColumn, char choices[9][12], int num) {
  const int nColumns = twoColumn ? 2 : 3;
  const int displayedChoices = nColumns * 3;
  for (int i = 0; i < displayedChoices; i++) {
    word_matrix[i] = i;
  }
  /* scramble matrix */
  random_permute((char *)word_matrix, displayedChoices);

  if (word_index % 4 == 0) {
    char desc[32] = "";
    int nr = (word_index / 4) + 1;
    format_number(desc, nr);
    layoutDialogSwipe(&bmp_icon_info, NULL, NULL, NULL, __("Please enter the"),
                      desc, __("of your recovery phrase"), NULL, NULL, NULL);
  } else {
    oledBox(0, 27, 127, 63, false);
  }

  for (int row = 0; row < 3; row++) {
    int y = 55 - row * 11;
    for (int col = 0; col < nColumns; col++) {
      int x = twoColumn ? 64 * col + 32 : 42 * col + 22;
      int choice = word_matrix[nColumns * row + col];
      const char *text = choice < num ? choices[choice] : "-";
      oledDrawString(x - oledStringWidth(text, FONT_STANDARD) / 2, y, text,
                     FONT_STANDARD);
      if (twoColumn) {
        oledInvert(x - 32 + 1, y - 1, x - 32 + 63 - 1, y + 8);
      } else {
        oledInvert(x - 22 + 1, y - 1, x - 22 + 41 - 1, y + 8);
      }
    }
  }
  oledRefresh();

  /* avoid picking out of range numbers */
  for (int i = 0; i < displayedChoices; i++) {
    if (word_matrix[i] >= num) word_matrix[i] = 0;
  }
  /* two column layout: middle column = right column */
  if (twoColumn) {
    static const uint8_t twolayout[9] = {0, 1, 1, 2, 3, 3, 4, 5, 5};
    for (int i = 8; i >= 2; i--) {
      word_matrix[i] = word_matrix[twolayout[i]];
    }
  }
}

/* Helper function for matrix recovery:
 * Generates a new matrix and requests the next pin.
 */
static void next_matrix(void) {
  char word_choices[9][12] = {0};
  uint32_t idx = 0, num = 0;
  bool last = (word_index % 4) == 3;

  /* Build the matrix:
   * num: number of choices
   * word_choices[][]: the strings containing the choices
   */
  switch (word_index % 4) {
    case 3:
      /* last level: show up to six words */
      /* idx: index in table2 for the entered choice. */
      /* first: the first word. */
      /* num: the number of words to choose from. */
      idx = TABLE1(word_pincode / 9) + word_pincode % 9;
      const uint32_t first = TABLE2(idx);
      num = TABLE2(idx + 1) - first;
      for (uint32_t i = 0; i < num; i++) {
        strlcpy(word_choices[i], mnemonic_get_word(first + i),
                sizeof(word_choices[i]));
      }
      break;

    case 2:
      /* third level: show up to nine ranges (using table2) */
      /* idx: first index in table2 corresponding to pin code. */
      /* num: the number of choices. */
      idx = TABLE1(word_pincode);
      num = TABLE1(word_pincode + 1) - idx;
      for (uint32_t i = 0; i < num; i++) {
        add_choice(word_choices[i], (word_table2[idx + i] >> 12),
                   mnemonic_get_word(TABLE2(idx + i)),
                   mnemonic_get_word(TABLE2(idx + i + 1) - 1));
      }
      break;

    case 1:
      /* second level: exactly nine ranges (using table1) */
      /* idx: first index in table1 corresponding to pin code. */
      /* num: the number of choices. */
      idx = word_pincode * 9;
      num = 9;
      for (uint32_t i = 0; i < num; i++) {
        add_choice(word_choices[i], (word_table1[idx + i] >> 12),
                   mnemonic_get_word(TABLE2(TABLE1(idx + i))),
                   mnemonic_get_word(TABLE2(TABLE1(idx + i + 1)) - 1));
      }
      break;

    case 0:
      /* first level: exactly nine ranges */
      /* num: the number of choices. */
      num = 9;
      for (uint32_t i = 0; i < num; i++) {
        add_choice(word_choices[i], 1, mnemonic_get_word(TABLE2(TABLE1(9 * i))),
                   mnemonic_get_word(TABLE2(TABLE1(9 * (i + 1))) - 1));
      }
      break;
  }
  display_choices(last, word_choices, num);

  recovery_request();
}

/* Function called when a digit was entered by user.
 * digit: ascii code of the entered digit ('1'-'9') or
 * '\x08' for backspace.
 */
static void recovery_digit(const char digit) {
  if (digit == 8) {
    /* backspace: undo */
    if ((word_index % 4) == 0) {
      /* undo complete word */
      if (word_index > 0) word_index -= 4;
    } else {
      word_index--;
      word_pincode /= 9;
    }
    next_matrix();
    return;
  }

  if (digit < '1' || digit > '9') {
    recovery_request();
    return;
  }

  int choice = word_matrix[digit - '1'];
  if ((word_index % 4) == 3) {
    /* received final word */

    /* Mark the chosen word for 250 ms */
    int y = 54 - ((digit - '1') / 3) * 11;
    int x = 64 * (((digit - '1') % 3) > 0);
    oledInvert(x + 1, y, x + 62, y + 9);
    oledRefresh();
    usbTiny(1);
    waitAndProcessUSBRequests(250);
    usbTiny(0);

    /* index of the chosen word */
    int idx = TABLE2(TABLE1(word_pincode / 9) + (word_pincode % 9)) + choice;
    uint32_t widx = word_index / 4;

    word_pincode = 0;
    strlcpy(words[widx], mnemonic_get_word(idx), sizeof(words[widx]));
    if (widx + 1 == word_count) {
      recovery_done(true);
      return;
    }
    /* next word */
  } else {
    word_pincode = word_pincode * 9 + choice;
  }
  word_index++;
  next_matrix();
}

/* Helper function for scrambled recovery:
 * Ask the user for the next word.
 */
void next_word(void) {
  layoutLast = layoutDialogSwipe;
  layoutSwipe();
  layoutHeader(_(T__ENTER_WORD));
  word_pos = word_order[word_index];
  if (word_pos == 0) {
    strlcpy(fake_word, mnemonic_get_word(random_uniform(BIP39_WORD_COUNT)),
            sizeof(fake_word));
    oledDrawStringCenterAdapter(OLED_WIDTH / 2, 24, fake_word,
                                FONT_FIXED | FONT_DOUBLE);
  } else {
    fake_word[0] = 0;
    char desc[32] = "";
    format_number(desc, word_pos);
    oledDrawStringCenterAdapter(OLED_WIDTH / 2, 24, desc, FONT_STANDARD);
  }
  oledRefresh();
  recovery_request();
}

void recovery_init(uint32_t _word_count, bool passphrase_protection,
                   bool pin_protection, const char *language, const char *label,
                   bool _enforce_wordlist, uint32_t type, uint32_t u2f_counter,
                   bool _dry_run) {
  if (_word_count != 12 && _word_count != 18 && _word_count != 24) return;

  recovery_mode = RECOVERY_NONE;
  word_pincode = 0;
  word_index = 0;
  word_count = _word_count;
#if !EMULATOR
  _enforce_wordlist = true;
#endif
  enforce_wordlist = _enforce_wordlist;
  dry_run = _dry_run;

  if (!dry_run) {
    layoutDialogCenterAdapterV2(
        _(T__IMPORT_WALLET), NULL, &bmp_bottom_left_close,
        &bmp_bottom_right_arrow, NULL, NULL, NULL, NULL, NULL, NULL,
        _(C__RESTORE_THE_WALLET_YOU_PREVIOUSLY_USED_FROM_A_RECOVERY_PHRASE));
    if (!protectButton(ButtonRequestType_ButtonRequest_ProtectCall, false)) {
      fsm_sendFailure(FailureType_Failure_ActionCancelled, NULL);
      layoutHome();
      return;
    }
  }

  if (!dry_run) {
    if (pin_protection && !protectChangePin(false)) {
      layoutHome();
      return;
    }

    config_setPassphraseProtection(passphrase_protection);
    config_setLanguage(language);
    config_setLabel(label);
    config_setU2FCounter(u2f_counter);
  }

  // Prefer matrix recovery if the host supports it.
  if ((type & RecoveryDeviceType_RecoveryDeviceType_Matrix) != 0) {
    recovery_mode = RECOVERY_MATRIX;
    next_matrix();
  } else {
    for (uint32_t i = 0; i < word_count; i++) {
      word_order[i] = i + 1;
    }
    for (uint32_t i = word_count; i < 24; i++) {
      word_order[i] = 0;
    }
    random_permute(word_order, 24);
    recovery_mode = RECOVERY_SCRAMBLED;
    next_word();
  }
}

static void recovery_scrambledword(const char *word) {
  int index = -1;
  if (enforce_wordlist) {  // check if word is valid
    index = mnemonic_find_word(word);
    if (index < 0) {  // not found
      if (!dry_run) {
        session_clear(true);
      }
      fsm_sendFailure(FailureType_Failure_DataError,
                      "Word not found in a wordlist");
      recovery_abort();
      return;
    }
  }

  if (word_pos != 0) {  // ignore fake words
    strlcpy(words[word_pos - 1], word, sizeof(words[word_pos - 1]));
  }

  if (word_index + 1 == 24) {  // last one
    recovery_done(true);
  } else {
    word_index++;
    next_word();
  }
}

/* Function called when a word was entered by user. Used
 * for scrambled recovery.
 */
void recovery_word(const char *word) {
  switch (recovery_mode) {
    case RECOVERY_MATRIX:
      recovery_digit(word[0]);
      break;
    case RECOVERY_SCRAMBLED:
      recovery_scrambledword(word);
      break;
    default:
      fsm_sendFailure(FailureType_Failure_UnexpectedMessage,
                      "Not in Recovery mode");
      break;
  }
}

/* Abort recovery.
 */
void recovery_abort(void) {
  memzero(words, sizeof(words));
  word_pincode = 0;
  if (recovery_mode != RECOVERY_NONE) {
    layoutHome();
    recovery_mode = RECOVERY_NONE;
  }
}

#define CANDIDATE_MAX_LEN 8  // DO NOT CHANGE THIS

static void select_complete_word(char *title, int start, int len) {
  uint8_t key = KEY_NULL;
  int index = 0;

#if !EMULATOR
  enableLongPress(false);
#endif

refresh_menu:
  layoutItemsSelectAdapterWords(
      &bmp_bottom_middle_arrow_up, &bmp_bottom_middle_arrow_down,
      &bmp_bottom_left_arrow, &bmp_bottom_right_confirm, NULL, NULL, index + 1,
      len, title, mnemonic_get_word(start + index),
      mnemonic_get_word(start + index),
      index > 0 ? mnemonic_get_word(start + index - 1) : NULL,
      index > 1 ? mnemonic_get_word(start + index - 2) : NULL,
      index > 2 ? mnemonic_get_word(start + index - 3) : NULL,
      index < len - 1 ? mnemonic_get_word(start + index + 1) : NULL,
      index < len - 2 ? mnemonic_get_word(start + index + 2) : NULL,
      index < len - 3 ? mnemonic_get_word(start + index + 3) : NULL, true,
      true);

  key = protectWaitKey(0, 0);
  switch (key) {
    case KEY_DOWN:
      if (index < len - 1) index++;
      goto refresh_menu;
    case KEY_UP:
      if (index > 0) index--;
      goto refresh_menu;
    case KEY_CONFIRM:
      strlcpy(words[word_index], mnemonic_get_word(start + index),
              sizeof(words[word_index]));
      word_index++;
      break;
    case KEY_CANCEL:
      break;
    default:
      break;
  }

#if !EMULATOR
  enableLongPress(true);
#endif
}

static uint8_t recovery_check_words(void) {
  uint8_t key = KEY_NULL;
  char desc[64] = "";
  uint32_t pages, index = 0;

  pages = word_count / 6;

refresh_menu:
  memzero(desc, sizeof(desc));
  strcat(desc, _(T__WORDLIST_BRACKET_STR_BRACKET));
  if (index == 0) {
    bracket_replace(desc, "1-6");
  } else if (index == 1) {
    bracket_replace(desc, "7-12");
  } else if (index == 2) {
    bracket_replace(desc, "13-18");
  } else {
    bracket_replace(desc, "19-24");
  }
  if (index == pages - 1) {
    layoutWords(
        desc, &bmp_bottom_middle_arrow_up, &bmp_bottom_middle_arrow_down,
        &bmp_bottom_left_arrow, &bmp_bottom_right_confirm, index + 1, pages,
        words[index * 6 + 0], words[index * 6 + 1], words[index * 6 + 2],
        words[index * 6 + 3], words[index * 6 + 4], words[index * 6 + 5]);
  } else {
    layoutWords(
        desc, &bmp_bottom_middle_arrow_up, &bmp_bottom_middle_arrow_down,
        &bmp_bottom_left_arrow, &bmp_bottom_right_arrow_off, index + 1, pages,
        words[index * 6 + 0], words[index * 6 + 1], words[index * 6 + 2],
        words[index * 6 + 3], words[index * 6 + 4], words[index * 6 + 5]);
  }
  key = protectWaitKey(0, 0);
  switch (key) {
    case KEY_UP:
      if (index > 0) index--;
      goto refresh_menu;
    case KEY_CANCEL:
      return 2;
    case KEY_DOWN:
      if (index < pages - 1) index++;
      goto refresh_menu;
    case KEY_CONFIRM:
      if (index < pages - 1) {
        index++;
        goto refresh_menu;
      } else {
        char new_mnemonic[MAX_MNEMONIC_LEN + 1] = {0};

        strlcpy(new_mnemonic, words[0], sizeof(new_mnemonic));
        for (uint32_t i = 1; i < word_count; i++) {
          strlcat(new_mnemonic, " ", sizeof(new_mnemonic));
          strlcat(new_mnemonic, words[i], sizeof(new_mnemonic));
        }
        if (!mnemonic_check(new_mnemonic)) return 1;
      }
      return 0;
    default:
      break;
  }

  return 1;
}

static bool input_words(void) {
  uint32_t prefix_len = 0;
  uint8_t key = KEY_NULL;
  char letter_list[52] = "";
  int index = 0;
  int letter_count = 0;
  char desc[64] = "", num_str[8] = {0};
  char title[13] = "";
  char last_letter = 0;
  bool ret = false;
  bool d = false;
  config_getInputDirection(&d);

  memzero(words[word_index], sizeof(words[word_index]));

#if !EMULATOR
  enableLongPress(true);
#endif

refresh_menu:
  memzero(desc, sizeof(desc));
  strcat(desc, _(T__ENTER_WORD_SHARP_STR));
  uint2str(word_index + 1, num_str);
  bracket_replace(desc, num_str);

  memzero(letter_list, sizeof(letter_list));
  letter_count = mnemonic_next_letter_with_prefix(words[word_index], prefix_len,
                                                  letter_list);
  layoutInputWord(desc, prefix_len, words[word_index], letter_list + 2 * index);
  key = protectWaitKey(0, 0);
#if !EMULATOR
  if (isLongPress(KEY_UP_OR_DOWN) && getLongPressStatus()) {
    if (isLongPress(KEY_UP)) {
      key = KEY_UP;
    } else if (isLongPress(KEY_DOWN)) {
      key = KEY_DOWN;
    }
    delay_ms(75);
  }
  if (d) {  // Reverse direction
    if (key == KEY_UP) {
      key = KEY_DOWN;
    } else if (key == KEY_DOWN) {
      key = KEY_UP;
    }
  }
#endif
  switch (key) {
    case KEY_DOWN:
      if (index < letter_count - 1)
        index++;
      else
        index = 0;
      goto refresh_menu;
    case KEY_UP:
      if (index > 0)
        index--;
      else
        index = letter_count - 1;
      goto refresh_menu;
    case KEY_CONFIRM:
      words[word_index][prefix_len++] = letter_list[2 * index];
      letter_count = mnemonic_count_with_prefix(words[word_index], prefix_len);
      if (letter_count > CANDIDATE_MAX_LEN) {
        index = 0;
        goto refresh_menu;
      } else {
        uint32_t candidate_location =
            mnemonic_word_index_with_prefix(words[word_index], prefix_len);
        memset(title, 0, 13);
        strcat(title, words[word_index]);
        strcat(title, "_");
        select_complete_word(title, candidate_location, letter_count);
        if (word_index == word_count) {
          ret = true;
          goto __ret;
        } else {  // next word
          prefix_len = 0;
          index = 0;
          memzero(words[word_index], sizeof(words[word_index]));
          goto refresh_menu;
        }
      }
    case KEY_CANCEL:
      if (prefix_len > 0) {
        prefix_len--;
        last_letter = words[word_index][prefix_len];
        index = 0;
        words[word_index][prefix_len] = 0;

        letter_count = mnemonic_next_letter_with_prefix(
            words[word_index], prefix_len, letter_list);
        for (int i = 0; i < letter_count; i++) {
          if (letter_list[2 * i] == last_letter) {
            index = i;
            break;
          }
        }
      } else if (word_index > 0) {
        word_index--;
        prefix_len = 0;
        index = 0;
        memzero(words[word_index], sizeof(words[word_index]));
      } else {
        break;
      }
      goto refresh_menu;
    default:
      break;
  }

__ret:
#if !EMULATOR
  enableLongPress(false);
#endif
  return ret;
}

bool recovery_on_device(void) {
  char desc[128] = "";
  char num_str[8] = "";
  uint8_t ret, key = KEY_NULL;

  if (config_hasPin()) {
    uint8_t ui_language_bak = ui_language;
    config_wipe();
    ui_language = ui_language_bak;
    config_setLanguage(i18n_lang_keys[ui_language]);
  }
prompt_recovery:
  layoutDialogCenterAdapterV2(
      _(T__IMPORT_WALLET), NULL, &bmp_bottom_left_close,
      &bmp_bottom_right_arrow, NULL, NULL, NULL, NULL, NULL, NULL,
      _(C__RESTORE_THE_WALLET_YOU_PREVIOUSLY_USED_FROM_A_RECOVERY_PHRASE));
  key = protectWaitKey(0, 1);
  if (key != KEY_CONFIRM) {
    return false;
  }

select_mnemonic_count:
  if (!protectSelectMnemonicNumber(&word_count, true)) {
    goto_check(prompt_recovery);
  }

  memzero(desc, sizeof(desc));
  strcat(desc, _(C__ENTER_YOUR_STR_WORDS_RECOVERY_PHRASE_IN_ORDER));
  uint2str(word_count, num_str);
  bracket_replace(desc, num_str);

  layoutDialogCenterAdapterV2(_(T__ENTER_RECOVERY_PHRASE), NULL,
                              &bmp_bottom_left_arrow, &bmp_bottom_right_arrow,
                              NULL, NULL, NULL, NULL, NULL, NULL, desc);
  key = protectWaitKey(0, 1);
  if (key != KEY_CONFIRM) {
    goto_check(select_mnemonic_count);
  }

  word_index = 0;
  recovery_byself = true;
  enforce_wordlist = true;
  dry_run = false;

  if (!input_words()) {
    goto_check(select_mnemonic_count);
  }

  memzero(desc, sizeof(desc));
  strcat(
      desc,
      _(C__THE_NEXT_SCREEN_WILL_START_DISPLAY_THE_STR_WORDS_YOU_JUST_ENTERED));
  uint2str(word_count, num_str);
  bracket_replace(desc, num_str);
check_words_again:
  layoutDialogCenterAdapterV2(_(T__REVIEW_WORDLIST), NULL,
                              &bmp_bottom_left_close, &bmp_bottom_right_arrow,
                              NULL, NULL, NULL, NULL, NULL, NULL, desc);
  while (1) {
    key = protectWaitKey(0, 1);
    if (key == KEY_CONFIRM || key == KEY_CANCEL) {
      break;
    }
  }
  if (key != KEY_CONFIRM) {
    layoutDialogCenterAdapterV2(
        _(T__ABORT_IMPORT_QUES), NULL, &bmp_bottom_left_close,
        &bmp_bottom_right_confirm, NULL, NULL, NULL, NULL, NULL, NULL,
        _(C__ARE_YOU_SURE_TO_ABORT_THIS_PROCESS_QUES_ALL_PROGRESS_WILL_BE_LOST));
    key = protectWaitKey(0, 1);
    if (key == KEY_CONFIRM) {
      return false;
    } else {
      goto check_words_again;
    }
  }
check_word:
  ret = recovery_check_words();
  if (1 == ret) {
    layoutDialogCenterAdapterV2(
        NULL, &bmp_icon_error, NULL, &bmp_bottom_right_retry, NULL, NULL, NULL,
        NULL, NULL, NULL,
        _(C__INVALID_RECOVERY_PHRASE_EXCLAM_CHECK_AND_TRY_AGAIN));
    while (1) {
      key = protectWaitKey(120 * timer1s, 1);
      if (key == KEY_CONFIRM) {
        break;
      } else {
        break;
      }
    }
    goto_check(select_mnemonic_count);
  } else if (2 == ret) {
    goto_check(check_words_again);
  } else {
    layoutDialogCenterAdapterV2(
        NULL, &bmp_icon_ok, NULL, &bmp_bottom_right_arrow, NULL, NULL, NULL,
        NULL, NULL, NULL, _(C__AWESOME_EXCLAM_YOUR_WALLET_IS_RESTORED));
    while (1) {
      key = protectWaitKey(0, 1);
      if (key == KEY_CONFIRM) {
        break;
      }
    }
  }
  if (!protectChangePinOnDevice(true, true, false)) {
    goto_check(check_word);
  }

  recovery_done(false);

  recovery_byself = false;
  return true;
}

uint32_t get_mnemonic_number(char *mnemonic) {
  uint32_t i = 0, count = 0;

  while (mnemonic[i]) {
    if (mnemonic[i] == ' ') {
      count++;
    }
    i++;
  }
  count++;
  return count;
}

bool verify_words(uint32_t count) {
  word_count = count;
  recovery_byself = true;
  word_index = 0;
  enforce_wordlist = true;
  dry_run = true;

  if (!input_words()) {
    return false;
  }
  recovery_done(true);
  recovery_byself = false;
  return true;
}

#if DEBUG_LINK

const char *recovery_get_fake_word(void) { return fake_word; }

uint32_t recovery_get_word_pos(void) { return word_pos; }

#endif
