/**
 * rofi-hyprspace — Rofi plugin for Hyprland window switching.
 *
 * Filter modes (set via HYPRSPACE_FILTER env var):
 *   ws+mon  — current workspace AND current monitor (Alt+Tab default)
 *   ws      — current workspace, all monitors
 *   mon     — current monitor, all workspaces
 *   all     — all windows (equivalent to built-in window mode)
 *
 * Author: boris
 * License: MIT
 */

#include <rofi/mode.h>
#include <rofi/mode-private.h>
#include <rofi/rofi-icon-fetcher.h>
#include <rofi/helper.h>
#include <glib.h>
#include <gmodule.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declaration
Mode mode;

// ── Types ────────────────────────────────────────────────────────────────────

typedef enum {
    FILTER_WS_MON = 0,   // current workspace + current monitor (default)
    FILTER_WS     = 1,   // current workspace only
    FILTER_MON    = 2,   // current monitor only
    FILTER_ALL    = 3,   // all windows
} HyprspaceFilter;

typedef struct {
    char *address;
    char *class;
    char *title;
    char *display;
    uint32_t icon_uid;
} HyprWindow;

typedef struct {
    HyprWindow      *windows;
    unsigned int     count;
    HyprspaceFilter  filter;
} HyprspaceMode;

// ── Helpers ──────────────────────────────────────────────────────────────────

static char *run_hyprctl(const char *args) {
    const char *sig = g_getenv("HYPRLAND_INSTANCE_SIGNATURE");
    char cmd[512];
    if (sig && sig[0])
        snprintf(cmd, sizeof(cmd),
                 "HYPRLAND_INSTANCE_SIGNATURE=%s hyprctl %s 2>/dev/null", sig, args);
    else
        snprintf(cmd, sizeof(cmd), "hyprctl %s 2>/dev/null", args);
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;
    GString *buf = g_string_new(NULL);
    char tmp[4096];
    while (fgets(tmp, sizeof(tmp), fp))
        g_string_append(buf, tmp);
    pclose(fp);
    return g_string_free(buf, FALSE);
}

static int get_active_workspace(void) {
    char *json = run_hyprctl("activeworkspace -j");
    if (!json) return -1;
    int id = -1;
    char *p = strstr(json, "\"id\":");
    if (p) id = atoi(p + 5);
    g_free(json);
    return id;
}

static int get_active_monitor(void) {
    char *json = run_hyprctl("activewindow -j");
    if (!json) return 0;
    int mon = 0;
    char *p = strstr(json, "\"monitor\":");
    if (p) mon = atoi(p + 10);
    g_free(json);
    return mon;
}

static char *json_get_string(const char *obj, const char *key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(obj, search);
    if (!p) return g_strdup("");
    p += strlen(search);
    while (*p == ' ') p++;
    if (*p != '"') return g_strdup("");
    p++;
    const char *end = strchr(p, '"');
    if (!end) return g_strdup("");
    return g_strndup(p, end - p);
}

static int json_get_int(const char *obj, const char *key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(obj, search);
    if (!p) return -1;
    p += strlen(search);
    while (*p == ' ') p++;
    return atoi(p);
}

static int json_get_bool(const char *obj, const char *key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(obj, search);
    if (!p) return 0;
    p += strlen(search);
    while (*p == ' ') p++;
    return strncmp(p, "true", 4) == 0;
}

static HyprspaceFilter parse_filter(void) {
    const char *env = g_getenv("HYPRSPACE_FILTER");
    if (!env)              return FILTER_WS_MON;
    if (!strcmp(env, "ws+mon")) return FILTER_WS_MON;
    if (!strcmp(env, "ws"))     return FILTER_WS;
    if (!strcmp(env, "mon"))    return FILTER_MON;
    if (!strcmp(env, "all"))    return FILTER_ALL;
    return FILTER_WS_MON;
}

// Search .desktop files for the Icon= field matching a given window class.
// Searches: ~/.local/share/applications, /usr/local/share/applications,
//           /usr/share/applications
static char *find_desktop_icon(const char *class_name) {
    const char *dirs[] = {
        g_get_home_dir(),          // prepend later
        "/usr/local/share/applications",
        "/usr/share/applications",
        NULL
    };

    // Build local apps dir
    char local_dir[512];
    snprintf(local_dir, sizeof(local_dir), "%s/.local/share/applications",
             g_get_home_dir());

    const char *search_dirs[] = {
        local_dir,
        "/usr/local/share/applications",
        "/usr/share/applications",
        NULL
    };
    (void)dirs;

    // Try exact match first, then case-insensitive, then prefix match
    for (int i = 0; search_dirs[i]; i++) {
        GDir *dir = g_dir_open(search_dirs[i], 0, NULL);
        if (!dir) continue;

        const char *fname;
        char *best = NULL;

        while ((fname = g_dir_read_name(dir))) {
            if (!g_str_has_suffix(fname, ".desktop")) continue;

            // Check if filename (without .desktop) matches class
            char *base = g_strndup(fname, strlen(fname) - 8);
            int match = (g_ascii_strcasecmp(base, class_name) == 0);
            g_free(base);

            if (!match) continue;

            // Read Icon= line
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", search_dirs[i], fname);
            FILE *fp = fopen(path, "r");
            if (!fp) continue;

            char line[256];
            while (fgets(line, sizeof(line), fp)) {
                if (strncmp(line, "Icon=", 5) == 0) {
                    char *icon = g_strstrip(line + 5);
                    // Strip newline
                    icon[strcspn(icon, "\r\n")] = 0;
                    best = g_strdup(icon);
                    break;
                }
            }
            fclose(fp);
            if (best) break;
        }
        g_dir_close(dir);
        if (best) return best;
    }

    // Fallback: try class-desktop suffix
    return g_strdup_printf("%s-desktop", class_name);
}

// Get icon uid for a window, using desktop file lookup
static uint32_t get_icon_uid(const char *class_name, unsigned int size) {
    // Always try desktop file lookup first for correct icon name
    char *icon_name = find_desktop_icon(class_name);
    uint32_t uid = rofi_icon_fetcher_query(icon_name ? icon_name : class_name, size);
    g_free(icon_name);
    return uid;
}

// ── Mode functions ───────────────────────────────────────────────────────────

static int hyprspace_init(Mode *sw) {
    HyprspaceMode *pd = g_new0(HyprspaceMode, 1);
    sw->private_data = pd;
    pd->filter = parse_filter();

    int active_ws  = get_active_workspace();
    int active_mon = get_active_monitor();

    char *json = run_hyprctl("clients -j");
    if (!json) return TRUE;

    GArray *wins = g_array_new(FALSE, TRUE, sizeof(HyprWindow));

    const char *p = json;
    while ((p = strchr(p, '{')) != NULL) {
        int depth = 0;
        const char *start = p;
        const char *c = p;
        while (*c) {
            if (*c == '{') depth++;
            else if (*c == '}') { depth--; if (depth == 0) break; }
            c++;
        }
        if (depth != 0) break;

        char *obj = g_strndup(start, c - start + 1);

        char *ws_str = strstr(obj, "\"workspace\":");
        int ws_id = -1;
        if (ws_str) {
            char *id_str = strstr(ws_str, "\"id\":");
            if (id_str) ws_id = atoi(id_str + 5);
        }

        int monitor  = json_get_int(obj, "monitor");
        int floating = json_get_bool(obj, "floating");
        int mapped   = json_get_bool(obj, "mapped");

        // Apply filter
        int match = 0;
        switch (pd->filter) {
            case FILTER_WS_MON: match = (ws_id == active_ws && monitor == active_mon); break;
            case FILTER_WS:     match = (ws_id == active_ws); break;
            case FILTER_MON:    match = (monitor == active_mon); break;
            case FILTER_ALL:    match = 1; break;
        }

        if (match && mapped && !floating) {
            HyprWindow w = {0};
            w.address = json_get_string(obj, "address");
            w.class   = json_get_string(obj, "class");
            w.title   = json_get_string(obj, "title");

            char *short_class = strrchr(w.class, '.');
            const char *cls_display = short_class ? short_class + 1 : w.class;

            char title_buf[64];
            snprintf(title_buf, sizeof(title_buf), "%s", w.title);

            w.display  = g_strdup_printf("%s — %s", cls_display, title_buf);

            w.icon_uid = get_icon_uid(w.class, 32);

            g_array_append_val(wins, w);
        }

        g_free(obj);
        p = c + 1;
    }

    g_free(json);
    pd->count   = wins->len;
    pd->windows = (HyprWindow *)g_array_free(wins, FALSE);
    return TRUE;
}

static unsigned int hyprspace_get_num_entries(const Mode *sw) {
    HyprspaceMode *pd = sw->private_data;
    return pd ? pd->count : 0;
}

static void hyprspace_destroy(Mode *sw) {
    HyprspaceMode *pd = sw->private_data;
    if (!pd) return;
    for (unsigned int i = 0; i < pd->count; i++) {
        g_free(pd->windows[i].address);
        g_free(pd->windows[i].class);
        g_free(pd->windows[i].title);
        g_free(pd->windows[i].display);
    }
    g_free(pd->windows);
    g_free(pd);
    sw->private_data = NULL;
}

static ModeMode hyprspace_result(Mode *sw, int menu_retv,
                                  char **input __attribute__((unused)),
                                  unsigned int selected_line) {
    HyprspaceMode *pd = sw->private_data;
    if (menu_retv & MENU_OK) {
        const char *addr = pd->windows[selected_line].address;

        // Write selection to file so wrapper script can re-assert after
        // rofi closes and follow_mouse potentially overwrites focus.
        FILE *sel = fopen("/tmp/hyprspace-selection", "w");
        if (sel) { fprintf(sel, "%s\n", addr); fclose(sel); }

        const char *sig = g_getenv("HYPRLAND_INSTANCE_SIGNATURE");
        char cmd[512];
        if (sig && sig[0]) {
            snprintf(cmd, sizeof(cmd),
                     "HYPRLAND_INSTANCE_SIGNATURE=%s hyprctl dispatch focuswindow address:%s",
                     sig, addr);
        } else {
            snprintf(cmd, sizeof(cmd),
                     "hyprctl dispatch focuswindow address:%s", addr);
        }
        system(cmd);
        return MODE_EXIT;
    }
    if (menu_retv & MENU_CANCEL) return MODE_EXIT;
    return RELOAD_DIALOG;
}

static int hyprspace_token_match(const Mode *sw, rofi_int_matcher **tokens,
                                  unsigned int index) {
    HyprspaceMode *pd = sw->private_data;
    return helper_token_match(tokens, pd->windows[index].display);
}

static char *hyprspace_get_display_value(const Mode *sw,
                                          unsigned int selected_line,
                                          int *state,
                                          GList **attr_list __attribute__((unused)),
                                          int get_entry) {
    HyprspaceMode *pd = sw->private_data;
    *state = 0;
    if (!get_entry) return NULL;
    return g_strdup(pd->windows[selected_line].display);
}

static cairo_surface_t *hyprspace_get_icon(const Mode *sw,
                                            unsigned int selected_line,
                                            unsigned int height) {
    HyprspaceMode *pd = sw->private_data;
    HyprWindow *w = &pd->windows[selected_line];
    if (w->icon_uid == 0)
        w->icon_uid = get_icon_uid(w->class, height);
    return rofi_icon_fetcher_get(w->icon_uid);
}

static char *hyprspace_get_completion(const Mode *sw, unsigned int selected_line) {
    HyprspaceMode *pd = sw->private_data;
    return g_strdup(pd->windows[selected_line].display);
}

static Mode *hyprspace_create(void) { return &mode; }

// ── Mode definition ──────────────────────────────────────────────────────────

Mode mode = {
    .abi_version          = ABI_VERSION,
    .name                 = "hyprspace",
    .cfg_name_key         = "display-hyprspace",
    .display_name         = NULL,
    ._init                = hyprspace_init,
    ._destroy             = hyprspace_destroy,
    ._get_num_entries     = hyprspace_get_num_entries,
    ._result              = hyprspace_result,
    ._token_match         = hyprspace_token_match,
    ._get_display_value   = hyprspace_get_display_value,
    ._get_icon            = hyprspace_get_icon,
    ._get_completion      = hyprspace_get_completion,
    ._preprocess_input    = NULL,
    ._get_message         = NULL,
    .free                 = NULL,
    ._create              = hyprspace_create,
    .type                 = MODE_TYPE_SWITCHER,
};
