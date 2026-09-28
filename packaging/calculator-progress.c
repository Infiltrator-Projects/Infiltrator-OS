#include <gtk/gtk.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    GtkWidget *window;
    GtkWidget *title;
    GtkWidget *stage;
    GtkWidget *detail;
    GtkWidget *bar;
    GtkWidget *close_button;
    char status_path[4096];
    bool finished;
} Ui;

typedef struct {
    char state[32];
    int progress;
    char stage[256];
    char detail[1024];
} Status;

static void copy_value(char *destination, size_t size, const char *value)
{
    if (!destination || size == 0U) return;
    g_strlcpy(destination, value ? value : "", size);
}

static bool read_status(const char *path, Status *status)
{
    FILE *file = fopen(path, "r");
    if (!file) return false;
    memset(status, 0, sizeof(*status));
    status->progress = -1;
    char line[1400];
    while (fgets(line, sizeof(line), file)) {
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        char *equals = strchr(line, '=');
        if (!equals) continue;
        *equals++ = '\0';
        if (strcmp(line, "state") == 0) copy_value(status->state, sizeof(status->state), equals);
        else if (strcmp(line, "progress") == 0) status->progress = atoi(equals);
        else if (strcmp(line, "stage") == 0) copy_value(status->stage, sizeof(status->stage), equals);
        else if (strcmp(line, "detail") == 0) copy_value(status->detail, sizeof(status->detail), equals);
    }
    fclose(file);
    return status->state[0] != '\0';
}

static gboolean close_after_success(gpointer data)
{
    Ui *ui = data;
    if (GTK_IS_WIDGET(ui->window)) gtk_widget_destroy(ui->window);
    return G_SOURCE_REMOVE;
}

static gboolean refresh_status(gpointer data)
{
    Ui *ui = data;
    Status status;
    if (!read_status(ui->status_path, &status)) {
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(ui->bar));
        return G_SOURCE_CONTINUE;
    }

    if (status.progress >= 0 && status.progress <= 100)
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui->bar), (double)status.progress / 100.0);
    else
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(ui->bar));

    if (status.stage[0]) gtk_label_set_text(GTK_LABEL(ui->stage), status.stage);
    if (status.detail[0]) gtk_label_set_text(GTK_LABEL(ui->detail), status.detail);

    if (!ui->finished && strcmp(status.state, "complete") == 0) {
        ui->finished = true;
        gtk_label_set_text(GTK_LABEL(ui->title), "Calculator is ready");
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui->bar), 1.0);
        gtk_widget_show(ui->close_button);
        g_timeout_add_seconds(8U, close_after_success, ui);
    } else if (!ui->finished && strcmp(status.state, "failed") == 0) {
        ui->finished = true;
        gtk_label_set_text(GTK_LABEL(ui->title), "Calculator installation failed");
        gtk_widget_show(ui->close_button);
    }
    return G_SOURCE_CONTINUE;
}

static void close_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    Ui *ui = data;
    gtk_widget_destroy(ui->window);
}

int main(int argc, char **argv)
{
    if (argc != 2) return EXIT_FAILURE;
    gtk_init(&argc, &argv);

    Ui ui;
    memset(&ui, 0, sizeof(ui));
    g_strlcpy(ui.status_path, argv[1], sizeof(ui.status_path));

    ui.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ui.window), "Infiltrator OS");
    gtk_window_set_default_size(GTK_WINDOW(ui.window), 520, 190);
    gtk_window_set_resizable(GTK_WINDOW(ui.window), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(ui.window), 18);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(ui.window), box);

    ui.title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(ui.title), "<b>Optimising Calculator for this computer</b>");
    gtk_widget_set_halign(ui.title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), ui.title, FALSE, FALSE, 0);

    ui.stage = gtk_label_new("Preparing native installation…");
    gtk_widget_set_halign(ui.stage, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), ui.stage, FALSE, FALSE, 0);

    ui.bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(ui.bar), TRUE);
    gtk_box_pack_start(GTK_BOX(box), ui.bar, FALSE, FALSE, 0);

    ui.detail = gtk_label_new("You can keep using the computer while this finishes.");
    gtk_label_set_line_wrap(GTK_LABEL(ui.detail), TRUE);
    gtk_label_set_xalign(GTK_LABEL(ui.detail), 0.0F);
    gtk_box_pack_start(GTK_BOX(box), ui.detail, FALSE, FALSE, 0);

    ui.close_button = gtk_button_new_with_label("Close");
    gtk_widget_set_halign(ui.close_button, GTK_ALIGN_END);
    g_signal_connect(ui.close_button, "clicked", G_CALLBACK(close_clicked), &ui);
    gtk_box_pack_end(GTK_BOX(box), ui.close_button, FALSE, FALSE, 0);
    gtk_widget_hide(ui.close_button);

    g_signal_connect(ui.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_show_all(ui.window);
    gtk_widget_hide(ui.close_button);

    g_timeout_add(250U, refresh_status, &ui);
    (void)refresh_status(&ui);
    gtk_main();
    return EXIT_SUCCESS;
}
