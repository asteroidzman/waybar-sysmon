// waybar CFFI plugin: CPU + RAM usage. Bar pill: [ cpu%] [ ram%] with threshold
// colours; click for a popover with per-core CPU bars and a memory/swap
// breakdown. Reads /proc directly (no external tools), refreshed every 3s.
#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <stdio.h>
#include <string.h>

#include "waybar_cffi_module.h"
const size_t wbcffi_version = 1;

#define GLYPH_CPU "\xf3\xb0\xbb\xa0"   // 󰻠 nf-md-cpu-64-bit
#define GLYPH_RAM "\xf3\xb0\x8d\x9b"   // 󰍛 nf-md-memory
#define MAXCORES 64

typedef struct { unsigned long long total, idle; } CpuT;

typedef struct {
  GtkWidget *box, *cpu_ic, *cpu_l, *ram_ic, *ram_l, *popover;
  CpuT prev, prevc[MAXCORES]; int ncores;
  double cpu, core[MAXCORES];
  long mem_total, mem_avail, swap_total, swap_free;   // kB
  double ram; int interval; guint timer;
} Inst;

// ─── /proc parsing ───────────────────────────────────────────────────────────
static double cpu_delta(CpuT *prev, unsigned long long t, unsigned long long idle) {
  unsigned long long dt = t - prev->total, di = idle - prev->idle;
  prev->total = t; prev->idle = idle;
  if (dt == 0) return 0;
  return 100.0 * (double)(dt - di) / (double)dt;
}
static void read_cpu(Inst *self) {
  FILE *f = fopen("/proc/stat", "r");
  if (!f) return;
  char line[512]; self->ncores = 0;
  while (fgets(line, sizeof line, f)) {
    if (strncmp(line, "cpu", 3) != 0) break;
    unsigned long long v[10] = {0}; int cpuid = -1;
    int n;
    if (line[3] == ' ') n = sscanf(line + 4, "%llu %llu %llu %llu %llu %llu %llu %llu",
                                   &v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7]);
    else { if (sscanf(line, "cpu%d", &cpuid) != 1) continue;
           char *sp = strchr(line, ' ');
           n = sp ? sscanf(sp + 1, "%llu %llu %llu %llu %llu %llu %llu %llu",
                           &v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7]) : 0; }
    if (n < 4) continue;
    unsigned long long total = 0; for (int i = 0; i < 8; i++) total += v[i];
    unsigned long long idle = v[3] + v[4];
    if (cpuid < 0) self->cpu = cpu_delta(&self->prev, total, idle);
    else if (cpuid < MAXCORES) {
      self->core[cpuid] = cpu_delta(&self->prevc[cpuid], total, idle);
      if (cpuid + 1 > self->ncores) self->ncores = cpuid + 1;
    }
  }
  fclose(f);
}
static void read_mem(Inst *self) {
  FILE *f = fopen("/proc/meminfo", "r");
  if (!f) return;
  char k[64]; long v;
  self->mem_total = self->mem_avail = self->swap_total = self->swap_free = 0;
  while (fscanf(f, "%63[^:]: %ld kB\n", k, &v) == 2) {
    if (!strcmp(k, "MemTotal")) self->mem_total = v;
    else if (!strcmp(k, "MemAvailable")) self->mem_avail = v;
    else if (!strcmp(k, "SwapTotal")) self->swap_total = v;
    else if (!strcmp(k, "SwapFree")) self->swap_free = v;
  }
  fclose(f);
  if (self->mem_total > 0)
    self->ram = 100.0 * (double)(self->mem_total - self->mem_avail) / (double)self->mem_total;
}

// ─── threshold class ─────────────────────────────────────────────────────────
static void set_level(GtkWidget *w, double v, double warn, double danger) {
  GtkStyleContext *c = gtk_widget_get_style_context(w);
  gtk_style_context_remove_class(c, "warn");
  gtk_style_context_remove_class(c, "danger");
  if (v > danger) gtk_style_context_add_class(c, "danger");
  else if (v > warn) gtk_style_context_add_class(c, "warn");
}
static void update_bar(Inst *self) {
  char t[16];
  g_snprintf(t, sizeof t, "%.0f%%", self->cpu); gtk_label_set_text(GTK_LABEL(self->cpu_l), t);
  g_snprintf(t, sizeof t, "%.0f%%", self->ram); gtk_label_set_text(GTK_LABEL(self->ram_l), t);
  set_level(self->cpu_ic, self->cpu, 60, 80);
  set_level(self->ram_ic, self->ram, 75, 90);
}

// ─── popover ─────────────────────────────────────────────────────────────────
static gboolean on_pop_key(GtkWidget *w, GdkEventKey *e, gpointer d) {
  (void)d; if (e->keyval == GDK_KEY_Escape) { gtk_popover_popdown(GTK_POPOVER(w)); return TRUE; }
  return FALSE;
}
static GtkWidget *bar_row(const char *label, double pct, const char *val) {
  GtkWidget *r = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *l = gtk_label_new(label);
  gtk_widget_set_size_request(l, 64, -1); gtk_widget_set_halign(l, GTK_ALIGN_START);
  gtk_style_context_add_class(gtk_widget_get_style_context(l), "sm-lbl");
  GtkWidget *pb = gtk_progress_bar_new();
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pb), pct / 100.0);
  gtk_widget_set_hexpand(pb, TRUE); gtk_widget_set_valign(pb, GTK_ALIGN_CENTER);
  gtk_style_context_add_class(gtk_widget_get_style_context(pb), "sm-bar");
  if (pct > 90) gtk_style_context_add_class(gtk_widget_get_style_context(pb), "danger");
  else if (pct > 75) gtk_style_context_add_class(gtk_widget_get_style_context(pb), "warn");
  GtkWidget *v = gtk_label_new(val);
  gtk_widget_set_size_request(v, 60, -1); gtk_widget_set_halign(v, GTK_ALIGN_END);
  gtk_style_context_add_class(gtk_widget_get_style_context(v), "sm-val");
  gtk_box_pack_start(GTK_BOX(r), l, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(r), pb, TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(r), v, FALSE, FALSE, 0);
  return r;
}
static void rebuild_popover(Inst *self) {
  GtkWidget *old = gtk_bin_get_child(GTK_BIN(self->popover));
  if (old) gtk_widget_destroy(old);
  GtkWidget *v = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_start(v, 16); gtk_widget_set_margin_end(v, 16);
  gtk_widget_set_margin_top(v, 14); gtk_widget_set_margin_bottom(v, 14);
  gtk_widget_set_size_request(v, 340, -1);
  gtk_style_context_add_class(gtk_widget_get_style_context(v), "sm-pop");

  GtkWidget *ch = gtk_label_new("CPU"); gtk_widget_set_halign(ch, GTK_ALIGN_START);
  gtk_style_context_add_class(gtk_widget_get_style_context(ch), "sm-head");
  gtk_box_pack_start(GTK_BOX(v), ch, FALSE, FALSE, 0);
  char val[24]; g_snprintf(val, sizeof val, "%.1f%%", self->cpu);
  gtk_box_pack_start(GTK_BOX(v), bar_row("Total", self->cpu, val), FALSE, FALSE, 0);
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 4); gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
  for (int i = 0; i < self->ncores; i++) {
    char cl[16], cv[16];
    g_snprintf(cl, sizeof cl, "core %d", i); g_snprintf(cv, sizeof cv, "%.0f%%", self->core[i]);
    gtk_grid_attach(GTK_GRID(grid), bar_row(cl, self->core[i], cv), i % 2, i / 2, 1, 1);
  }
  gtk_box_pack_start(GTK_BOX(v), grid, FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(v), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);
  GtkWidget *mh = gtk_label_new("Memory"); gtk_widget_set_halign(mh, GTK_ALIGN_START);
  gtk_style_context_add_class(gtk_widget_get_style_context(mh), "sm-head");
  gtk_box_pack_start(GTK_BOX(v), mh, FALSE, FALSE, 0);
  double used_gb = (self->mem_total - self->mem_avail) / 1048576.0, tot_gb = self->mem_total / 1048576.0;
  g_snprintf(val, sizeof val, "%.1f/%.1fG", used_gb, tot_gb);
  gtk_box_pack_start(GTK_BOX(v), bar_row("RAM", self->ram, val), FALSE, FALSE, 0);
  if (self->swap_total > 0) {
    double sused = self->swap_total - self->swap_free;
    double sp = 100.0 * sused / self->swap_total;
    g_snprintf(val, sizeof val, "%.1f/%.1fG", sused / 1048576.0, self->swap_total / 1048576.0);
    gtk_box_pack_start(GTK_BOX(v), bar_row("Swap", sp, val), FALSE, FALSE, 0);
  }
  gtk_container_add(GTK_CONTAINER(self->popover), v);
  gtk_widget_show_all(v);
}
static gboolean on_click(GtkWidget *w, GdkEventButton *ev, gpointer data) {
  (void)w; if (ev->button != 1) return FALSE;
  Inst *self = data;
  rebuild_popover(self);
  gtk_popover_popup(GTK_POPOVER(self->popover));
  gtk_widget_grab_focus(self->popover);
  return TRUE;
}

static gboolean tick(gpointer data) { Inst *s = data; read_cpu(s); read_mem(s); update_bar(s); return G_SOURCE_CONTINUE; }

// ─── CFFI ────────────────────────────────────────────────────────────────────
static GtkWidget *mklabel(const char *txt, const char *cls) {
  GtkWidget *l = gtk_label_new(txt);
  gtk_style_context_add_class(gtk_widget_get_style_context(l), cls);
  return l;
}
void *wbcffi_init(const wbcffi_init_info *info, const wbcffi_config_entry *entries, size_t entries_len) {
  Inst *self = g_new0(Inst, 1);
  self->interval = 3;
  for (size_t i = 0; i < entries_len; i++)
    if (!strcmp(entries[i].key, "interval")) { self->interval = atoi(entries[i].value); if (self->interval < 1) self->interval = 1; }

  GtkContainer *root = info->get_root_widget(info->obj);
  self->box = gtk_event_box_new();
  gtk_widget_set_name(self->box, "sysmon");
  gtk_widget_add_events(self->box, GDK_BUTTON_PRESS_MASK);
  gtk_widget_set_margin_start(self->box, 6); gtk_widget_set_margin_end(self->box, 6);
  GtkWidget *h = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  self->cpu_ic = mklabel(GLYPH_CPU, "sm-cpu-icon");
  self->cpu_l  = mklabel("--%", "sm-cpu");
  self->ram_ic = mklabel(GLYPH_RAM, "sm-ram-icon");
  self->ram_l  = mklabel("--%", "sm-ram");
  gtk_box_pack_start(GTK_BOX(h), self->cpu_ic, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(h), self->cpu_l, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(h), self->ram_ic, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(h), self->ram_l, FALSE, FALSE, 0);
  gtk_container_add(GTK_CONTAINER(self->box), h);
  self->popover = gtk_popover_new(self->box);
  gtk_popover_set_position(GTK_POPOVER(self->popover), GTK_POS_BOTTOM);
  gtk_popover_set_constrain_to(GTK_POPOVER(self->popover), GTK_POPOVER_CONSTRAINT_NONE);
  gtk_popover_set_modal(GTK_POPOVER(self->popover), TRUE);
  gtk_widget_add_events(self->popover, GDK_KEY_PRESS_MASK);
  g_signal_connect(self->popover, "key-press-event", G_CALLBACK(on_pop_key), NULL);
  g_signal_connect(self->box, "button-press-event", G_CALLBACK(on_click), self);
  gtk_container_add(root, self->box);
  gtk_widget_show_all(GTK_WIDGET(root));

  read_cpu(self); read_mem(self);   // prime deltas
  self->timer = g_timeout_add_seconds(self->interval, tick, self);
  return self;
}
void wbcffi_deinit(void *instance) {
  Inst *self = instance;
  if (self->timer) g_source_remove(self->timer);
  g_free(self);
}
