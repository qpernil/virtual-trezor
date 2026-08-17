/*
 * Practical SDL controls for the Raspberry Pi validation worker.
 *
 * The upstream emulator accepts only the keyboard arrow keys. Keep those
 * mappings and additionally treat the left and right halves of the emulator
 * window as the physical No and Yes buttons.
 */

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

#include "buttons.h"

static SDL_atomic_t pending_no;
static SDL_atomic_t pending_yes;

static int capture_click(void *unused, SDL_Event *event) {
  (void)unused;
  if (event->type != SDL_MOUSEBUTTONUP ||
      event->button.button != SDL_BUTTON_LEFT) {
    return 1;
  }

  SDL_Window *window = SDL_GetWindowFromID(event->button.windowID);
  if (window == NULL) {
    return 1;
  }
  int width = 0;
  SDL_GetWindowSize(window, &width, NULL);
  if (event->button.x < width / 2) {
    SDL_AtomicSet(&pending_no, 1);
  } else {
    SDL_AtomicSet(&pending_yes, 1);
  }
  return 1;
}

uint16_t buttonRead(void) {
  static bool event_watch_installed = false;
  static uint8_t no_pulse = 0;
  static uint8_t yes_pulse = 0;
  if (!event_watch_installed) {
    SDL_AddEventWatch(capture_click, NULL);
    event_watch_installed = true;
  }
  if (SDL_AtomicCAS(&pending_no, 1, 0)) {
    no_pulse = 2;
  }
  if (SDL_AtomicCAS(&pending_yes, 1, 0)) {
    yes_pulse = 2;
  }

  uint16_t state = 0;

  const uint8_t *keys = SDL_GetKeyboardState(NULL);
  if (no_pulse == 2 || keys[SDL_SCANCODE_LEFT] ||
      keys[SDL_SCANCODE_ESCAPE]) {
    state |= BTN_PIN_NO;
  }
  if (yes_pulse == 2 || keys[SDL_SCANCODE_RIGHT] ||
      keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_SPACE]) {
    state |= BTN_PIN_YES;
  }

  if (no_pulse > 0) {
    no_pulse--;
  }
  if (yes_pulse > 0) {
    yes_pulse--;
  }

  return ~state;
}
