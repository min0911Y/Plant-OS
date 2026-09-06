#ifndef PLANT_OS_KEY_INPUT_H
#define PLANT_OS_KEY_INPUT_H

/* getch() returns characters/control characters or one of these logical keys.
 */
enum key_input {
  KEY_INPUT_UP = -1,
  KEY_INPUT_DOWN = -2,
  KEY_INPUT_LEFT = -3,
  KEY_INPUT_RIGHT = -4,
  KEY_INPUT_HOME = -5,
  KEY_INPUT_END = -6,
  KEY_INPUT_PAGE_UP = -7,
  KEY_INPUT_PAGE_DOWN = -8,
  KEY_INPUT_DELETE = -9,
  KEY_INPUT_INSERT = -10,
  KEY_INPUT_ESCAPE = 27,
};

#endif
