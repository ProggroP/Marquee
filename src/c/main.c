#include <pebble.h>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

#define KEY_TEXT             0
#define KEY_FONT             1
#define TEXT_BUFFER_SIZE   128
#define DEFAULT_TEXT      "Hello!"
#define DEFAULT_FONT_IDX   1

// Font index mapping:
//   0  ->  GOTHIC_18_BOLD   (klein)
//   1  ->  GOTHIC_24_BOLD   (mittel, default)
//   2  ->  BITHAM_42_BOLD   (groß)

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static Window *s_window;
static Layer  *s_canvas_layer;

static char s_text_buf[TEXT_BUFFER_SIZE];
static int  s_font_idx;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static GFont prv_get_font(int idx) {
  switch (idx) {
    case 0:  return fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    case 2:  return fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK);
    case 3:  return fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
    default: return fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  }
}

// ---------------------------------------------------------------------------
// Canvas layer
// ---------------------------------------------------------------------------

static void prv_canvas_update(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GFont font   = prv_get_font(s_font_idx);

  // Measure the rendered text height so we can center it vertically.
  GSize text_size = graphics_text_layout_get_content_size(
    s_text_buf, font, bounds,
    GTextOverflowModeWordWrap, GTextAlignmentCenter
  );

  int y = (bounds.size.h - text_size.h) / 2;
  if (y < 0) { y = 0; }

  GRect draw_rect = GRect(0, y, bounds.size.w, bounds.size.h - y);

  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(
    ctx, s_text_buf, font, draw_rect,
    GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL
  );
}

// ---------------------------------------------------------------------------
// AppMessage
// ---------------------------------------------------------------------------

static void prv_inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t_text = dict_find(iter, MESSAGE_KEY_TEXT);
  if (t_text) {
    snprintf(s_text_buf, sizeof(s_text_buf), "%s", t_text->value->cstring);
    persist_write_string(KEY_TEXT, s_text_buf);
  }

  Tuple *t_font = dict_find(iter, MESSAGE_KEY_FONT);
  if (t_font) {
    s_font_idx = atoi(t_font->value->cstring);
    persist_write_int(KEY_FONT, s_font_idx);
  }
    
  layer_mark_dirty(s_canvas_layer);
}

// ---------------------------------------------------------------------------
// Window lifecycle
// ---------------------------------------------------------------------------

static void prv_window_load(Window *window) {
  Layer *root   = window_get_root_layer(window);
  GRect  bounds = layer_get_bounds(root);

  window_set_background_color(window, GColorWhite);

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, prv_canvas_update);
  layer_add_child(root, s_canvas_layer);
}

static void prv_window_unload(Window *window) {
  layer_destroy(s_canvas_layer);
}

// ---------------------------------------------------------------------------
// Init / Deinit
// ---------------------------------------------------------------------------

static void prv_init(void) {
  // Restore persisted values, or use defaults.
  if (persist_exists(KEY_TEXT)) {
    persist_read_string(KEY_TEXT, s_text_buf, sizeof(s_text_buf));
  } else {
    snprintf(s_text_buf, sizeof(s_text_buf), "%s", DEFAULT_TEXT);
  }
  s_font_idx = persist_exists(KEY_FONT)
             ? persist_read_int(KEY_FONT)
             : DEFAULT_FONT_IDX;

  // Register AppMessage handler.
  app_message_register_inbox_received(prv_inbox_received);
  // Feste Puffergröße statt _maximum(): auf manchen Firmwares gibt
  // app_message_inbox_size_maximum() 0 zurück, was AppMessage stumm bricht.
  app_message_open(256, 64);

  // Create and show window.
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);
}

static void prv_deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}