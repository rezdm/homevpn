#include "HomeVPNCore.h"
#include <gtk/gtk.h>
#include <libayatana-appindicator/app-indicator.h>
#include <memory>

class HomeVPN_GUI {
private:
    std::unique_ptr<HomeVPNCore> core_;
    
    GtkApplication *app_;
    GtkWidget *window_{};
    GtkWidget *vpn_switch_{};
    GtkWidget *mount_switch_{};
    GtkWidget *log_textview_{};
    GtkTextBuffer *log_buffer_{};
    AppIndicator *indicator_{};

public:
    explicit HomeVPN_GUI(GtkApplication *application) : app_(application) {
        core_ = std::make_unique<HomeVPNCore>();
        core_->loadConfig();
        
        // Set up callbacks
        core_->setStatusCallback([this](const HomeVPNCore::Status& status) {
            g_idle_add([](gpointer user_data) -> gboolean {
                static_cast<HomeVPN_GUI*>(user_data)->onStatusUpdate();
                return G_SOURCE_REMOVE;
            }, this);
        });
        
        core_->setLogCallback([this](const std::string& message) {
            g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, [](gpointer user_data) -> gboolean {
                auto* data = static_cast<std::pair<HomeVPN_GUI*, std::string>*>(user_data);
                data->first->onLogMessage(data->second);
                delete data;
                return G_SOURCE_REMOVE;
            }, new std::pair<HomeVPN_GUI*, std::string>(this, message), nullptr);
        });
        
        createWindow();
        createTrayIndicator();
        
        // Start monitoring
        core_->updateStatus();
        core_->startStatusMonitor();
    }
    
    ~HomeVPN_GUI() {
        core_->stopStatusMonitor();
    }

private:
    void createWindow() {
        window_ = gtk_application_window_new(app_);
        gtk_window_set_title(GTK_WINDOW(window_), "homeVPN");
        gtk_window_set_default_size(GTK_WINDOW(window_), 400, 300);
        gtk_window_set_resizable(GTK_WINDOW(window_), FALSE);

        // Connect window close event to hide instead of quit
        g_signal_connect(window_, "delete-event", G_CALLBACK(onWindowDelete), this);

        // Create main container
        GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
        gtk_container_add(GTK_CONTAINER(window_), vbox);

        // VPN Control
        GtkWidget *vpn_frame = gtk_frame_new("VPN Connection");
        GtkWidget *vpn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_container_set_border_width(GTK_CONTAINER(vpn_box), 10);

        GtkWidget *vpn_label = gtk_label_new("VPN Status:");
        vpn_switch_ = gtk_switch_new();
        g_signal_connect(vpn_switch_, "notify::active", G_CALLBACK(onVPNToggle), this);

        gtk_box_pack_start(GTK_BOX(vpn_box), vpn_label, FALSE, FALSE, 0);
        gtk_box_pack_end(GTK_BOX(vpn_box), vpn_switch_, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(vpn_frame), vpn_box);
        gtk_box_pack_start(GTK_BOX(vbox), vpn_frame, FALSE, FALSE, 0);

        // Mount Control
        GtkWidget *mount_frame = gtk_frame_new("Network Share");
        GtkWidget *mount_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_container_set_border_width(GTK_CONTAINER(mount_box), 10);

        GtkWidget *mount_label = gtk_label_new("Mount Status:");
        mount_switch_ = gtk_switch_new();
        gtk_widget_set_sensitive(mount_switch_, FALSE);
        g_signal_connect(mount_switch_, "notify::active", G_CALLBACK(onMountToggle), this);

        gtk_box_pack_start(GTK_BOX(mount_box), mount_label, FALSE, FALSE, 0);
        gtk_box_pack_end(GTK_BOX(mount_box), mount_switch_, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(mount_frame), mount_box);
        gtk_box_pack_start(GTK_BOX(vbox), mount_frame, FALSE, FALSE, 0);

        // Log area
        GtkWidget *log_frame = gtk_frame_new("Log");
        GtkWidget *scrolled = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_widget_set_size_request(scrolled, -1, 150);

        log_textview_ = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(log_textview_), FALSE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(log_textview_), GTK_WRAP_WORD);
        log_buffer_ = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_textview_));

        gtk_container_add(GTK_CONTAINER(scrolled), log_textview_);
        gtk_container_add(GTK_CONTAINER(log_frame), scrolled);
        gtk_box_pack_start(GTK_BOX(vbox), log_frame, TRUE, TRUE, 0);

        gtk_widget_show_all(window_);
    }

    void createTrayIndicator() {
        indicator_ = app_indicator_new("homevpn", "network-offline",
                                     APP_INDICATOR_CATEGORY_SYSTEM_SERVICES);
        app_indicator_set_status(indicator_, APP_INDICATOR_STATUS_ACTIVE);

        // Create menu
        GtkWidget *menu = gtk_menu_new();

        GtkWidget *show_item = gtk_menu_item_new_with_label("Show Window");
        g_signal_connect(show_item, "activate", G_CALLBACK(onShowWindow), this);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), show_item);

        GtkWidget *quit_item = gtk_menu_item_new_with_label("Quit");
        g_signal_connect(quit_item, "activate", G_CALLBACK(onQuit), this);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);

        gtk_widget_show_all(menu);
        app_indicator_set_menu(indicator_, GTK_MENU(menu));
    }
    
    void onStatusUpdate() {
        const auto& status = core_->getStatus();
        
        // Block signals temporarily to prevent recursion
        g_signal_handlers_block_by_func(vpn_switch_, (gpointer)onVPNToggle, this);
        g_signal_handlers_block_by_func(mount_switch_, (gpointer)onMountToggle, this);
        
        // Update switches
        gtk_switch_set_active(GTK_SWITCH(vpn_switch_), status.vpn_connected);
        gtk_switch_set_active(GTK_SWITCH(mount_switch_), status.share_mounted);
        gtk_widget_set_sensitive(mount_switch_, status.vpn_connected);
        
        // Update tray icon
        const char* icon = status.vpn_connected ? "network-vpn" : "network-offline";
        app_indicator_set_icon(indicator_, icon);
        
        // Unblock signals
        g_signal_handlers_unblock_by_func(vpn_switch_, (gpointer)onVPNToggle, this);
        g_signal_handlers_unblock_by_func(mount_switch_, (gpointer)onMountToggle, this);
    }
    
    void onLogMessage(const std::string& message) {
        GtkTextIter end_iter;
        gtk_text_buffer_get_end_iter(log_buffer_, &end_iter);
        
        std::string msg = message + "\n";
        gtk_text_buffer_insert(log_buffer_, &end_iter, msg.c_str(), -1);
        
        // Auto-scroll to bottom
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(log_textview_), &end_iter,
                                   0.0, FALSE, 0.0, 0.0);
    }

    // Static callback functions
    static gboolean onWindowDelete(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
        gtk_widget_hide(widget);
        return TRUE;
    }

    static void onShowWindow(GtkMenuItem *menuitem, gpointer user_data) {
        auto *gui = static_cast<HomeVPN_GUI*>(user_data);
        gtk_widget_show(gui->window_);
        gtk_window_present(GTK_WINDOW(gui->window_));
    }

    static void onQuit(GtkMenuItem *menuitem, gpointer user_data) {
        auto *gui = static_cast<HomeVPN_GUI*>(user_data);
        g_application_quit(G_APPLICATION(gui->app_));
    }

    static void onVPNToggle(GObject *object, GParamSpec *pspec, gpointer user_data) {
        auto *gui = static_cast<HomeVPN_GUI*>(user_data);
        gboolean active = gtk_switch_get_active(GTK_SWITCH(object));
        
        if (active) {
            gui->core_->connectVPN();
        } else {
            gui->core_->disconnectVPN();
        }
    }

    static void onMountToggle(GObject *object, GParamSpec *pspec, gpointer user_data) {
        auto *gui = static_cast<HomeVPN_GUI*>(user_data);
        gboolean active = gtk_switch_get_active(GTK_SWITCH(object));
        
        if (active) {
            gui->core_->mountShare();
        } else {
            gui->core_->unmountShare();
        }
    }
};

static void activate(GtkApplication *app, gpointer user_data) {
    new HomeVPN_GUI(app);
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("com.homevpn.gui", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);
    return status;
}

