#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>

class HomeVPNCore {
public:
    struct Config {
        std::string vpn_connect_cmd = "echo 'VPN Connect'";
        std::string vpn_disconnect_cmd = "echo 'VPN Disconnect'";
        std::string mount_cmd = "echo 'Mount'";
        std::string unmount_cmd = "echo 'Unmount'";
        std::string check_ip_url = "https://ipinfo.io/ip";
        std::string expected_ip = "";
        std::string home_ip_prefix = "192.168.1.";
        int status_check_interval = 30; // seconds
    };

    struct Status {
        bool vpn_connected = false;
        bool share_mounted = false;
        std::string current_ip = "";
        std::string last_error = "";
    };

    // Callback types for UI notifications
    using StatusCallback = std::function<void(const Status&)>;
    using LogCallback = std::function<void(const std::string&)>;

    explicit HomeVPNCore();
    ~HomeVPNCore();

    // Configuration
    bool loadConfig(const std::string& config_path = "");
    void saveConfig(const std::string& config_path = "");
    const Config& getConfig() const { return config_; }
    void setConfig(const Config& config) { config_ = config; }

    // Core operations
    void connectVPN();
    void disconnectVPN();
    void mountShare();
    void unmountShare();
    void updateStatus();
    
    // Status monitoring
    void startStatusMonitor();
    void stopStatusMonitor();
    
    // Status access
    const Status& getStatus() const { return status_; }
    
    // Logging
    const std::vector<std::string>& getLogs() const;
    void clearLogs();
    
    // UI callbacks
    void setStatusCallback(StatusCallback callback);
    void setLogCallback(LogCallback callback);
    
    // Utility functions
    static std::string getCurrentTimestamp();
    
private:
    Config config_;
    Status status_;
    std::vector<std::string> logs_;
    mutable std::mutex logs_mutex_;
    mutable std::mutex status_mutex_;
    
    StatusCallback status_callback_;
    LogCallback log_callback_;
    
    std::thread monitor_thread_;
    std::atomic<bool> monitor_running_{false};
    
public:
    void addLog(const std::string& message);

private:
    std::string executeCommand(const std::string& command);
    std::string getExternalIP();
    bool checkVPNConnection();
    bool checkShareMount();
    void notifyStatusChange();
    void statusMonitorLoop();
    
    // HTTP helper
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp);
};

