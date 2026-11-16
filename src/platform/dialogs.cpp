#include "screencapture/platform/dialogs.h"
#include <gtk/gtk.h>

namespace sc::platform {

bool showInfoDialogWithCheckbox(const std::string& title, const std::string& message,
                                const std::string& checkboxText) {
    GtkWidget* dlg = gtk_message_dialog_new(
        nullptr,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_OK,
        "%s",
        message.c_str());
    gtk_window_set_title(GTK_WINDOW(dlg), title.c_str());
    gtk_window_set_keep_above(GTK_WINDOW(dlg), TRUE);
    gtk_window_set_position(GTK_WINDOW(dlg), GTK_WIN_POS_CENTER);

    GtkWidget* check = gtk_check_button_new_with_label(checkboxText.c_str());
    gtk_box_pack_start(GTK_BOX(gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dlg))),
                       check, FALSE, FALSE, 0);

    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    bool checked = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check));
    // Hide before destroy: gtk_widget_hide queues XUnmapWindow on the display
    // connection right away, so a same-process screen capture that follows
    // (the startup pre-capture) can never include the dialog, even though GDK
    // may defer the actual XDestroyWindow to a later main-loop pass.
    gtk_widget_hide(dlg);
    gtk_widget_destroy(dlg);
    return checked;
}

} // namespace sc::platform
