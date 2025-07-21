#include "HomeVPNCore.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <curl/curl.h>
#include <unistd.h>
#include <sys/wait.h>

HomeVPNCore::HomeVPNCore() {
    // Initialize curl globally
    static bool curl_initialized = false;
    if (!curl_initialized) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_initialized = true;
    }
}

HomeVPNCore::~HomeVPNCore() {
    stopStatusMonitor();
}

bool HomeVPNCore::loadConfig(const std::string& config_path) {
    std::string path = config_path;
    if (path.empty()) {
        const char* home = getenv("HOME");
        if (!home) return false;
        path = std::string(home) + "/.homeVPN";
    }
    
    std::ifstream file(path);
    if (!file.is_open()) {
        addLog("Config file not found, using defaults: " + path);
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        
        std::string key = line.substr(0, equals);
        std::string value = line.substr(equals + 1);
        
        // Remove quotes if present
        if (value.length() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.length() - 2);
        }
        
        // Set configuration values
        if (key == "vpn_connect_cmd" || key == "vpn_connect") {
            config_.vpn_connect_cmd = value;
        } else if (key == "vpn_disconnect_cmd" || key == "vpn_disconnect") {
            config_.vpn_disconnect_cmd = value;
        } else if (key == "mount_cmd") {
            config_.mount_cmd = value;
        } else if (key == "unmount_cmd") {
            config_.unmount_cmd = value;
        } else if (key == "check_ip_url") {
            config_.check_ip_url = value;
        } else if (key == "expected_ip") {
            config_.expected_ip = value;
        } else if (key == "home_ip" || key == "home_ip_prefix") {
            config_.home_ip_prefix = value;
        } else if (key == "status_check_interval") {
            try {
                config_.status_check_interval = std::stoi(value);
            } catch (...) {
                addLog("Invalid status_check_interval value: " + value);
            }
        }
    }
    
    addLog("Configuration loaded from: " + path);
    return true;
}

void HomeVPNCore::saveConfig(const std::string& config_path) {
    std::string path = config_path;
    if (path.empty()) {
        const char* home = getenv("HOME");
        if (!home) return;
        path = std::string(home) + "/.homeVPN";
    }
    
    std::ofstream file(path);
    if (!file.is_open()) {
        addLog("Failed to save config to: " + path);
        return;
    }
    
    file << "# HomeVPN Configuration\n";
    file << "vpn_connect_cmd=" << config_.vpn_connect_cmd << "\n";
    file << "vpn_disconnect_cmd=" << config_.vpn_disconnect_cmd << "\n";
    file << "mount_cmd=" << config_.mount_cmd << "\n";
    file << "unmount_cmd=" << config_.unmount_cmd << "\n";
    file << "check_ip_url=" << config_.check_ip_url << "\n";
    file << "expected_ip=" << config_.expected_ip << "\n";
    file << "home_ip_prefix=" << config_.home_ip_prefix << "\n";
    file << "status_check_interval=" << config_.status_check_interval << "\n";
    
    addLog("Configuration saved to: " + path);
}

void HomeVPNCore::connectVPN() {
    addLog("Connecting to VPN...");
    std::string result = executeCommand(config_.vpn_connect_cmd);
    
    // Wait a moment for connection to establish
    std::this_thread::sleep_for(std::chrono::seconds(2));
    updateStatus();
}

void HomeVPNCore::disconnectVPN() {
    addLog("Disconnecting from VPN...");
    std::string result = executeCommand(config_.vpn_disconnect_cmd);
    
    // Wait a moment for disconnection
    std::this_thread::sleep_for(std::chrono::seconds(1));
    updateStatus();
}

void HomeVPNCore::mountShare() {
    if (!status_.vpn_connected) {
        addLog("ERROR: Cannot mount share - VPN not connected");
        status_.last_error = "VPN not connected";
        notifyStatusChange();
        return;
    }
    
    addLog("Mounting network share...");
    std::string result = executeCommand(config_.mount_cmd);
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    updateStatus();
}

void HomeVPNCore::unmountShare() {
    addLog("Unmounting network share...");
    std::string result = executeCommand(config_.unmount_cmd);
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    updateStatus();
}

void HomeVPNCore::updateStatus() {
    std::lock_guard<std::mutex> lock(status_mutex_);
    
    Status old_status = status_;
    
    // Check VPN connection
    status_.current_ip = getExternalIP();
    status_.vpn_connected = checkVPNConnection();
    
    // Check share mount
    status_.share_mounted = checkShareMount();
    
    // If VPN disconnected, disable mount
    if (!status_.vpn_connected && status_.share_mounted) {
        // Try to unmount
        executeCommand(config_.unmount_cmd);
        status_.share_mounted = false;
        addLog("VPN disconnected, unmounting share");
    }
    
    // Clear error if status improved
    if (status_.vpn_connected && !old_status.vpn_connected) {
        status_.last_error = "";
    }
    
    // Log status changes
    if (old_status.vpn_connected != status_.vpn_connected) {
        addLog(status_.vpn_connected ? "VPN Connected" : "VPN Disconnected");
    }
    
    if (old_status.share_mounted != status_.share_mounted) {
        addLog(status_.share_mounted ? "Share Mounted" : "Share Unmounted");
    }
    
    notifyStatusChange();
}

void HomeVPNCore::startStatusMonitor() {
    if (monitor_running_.load()) return;
    
    monitor_running_.store(true);
    monitor_thread_ = std::thread(&HomeVPNCore::statusMonitorLoop, this);
    addLog("Status monitor started");
}

void HomeVPNCore::stopStatusMonitor() {
    if (!monitor_running_.load()) return;
    
    monitor_running_.store(false);
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    addLog("Status monitor stopped");
}

const std::vector<std::string>& HomeVPNCore::getLogs() const {
    std::lock_guard<std::mutex> lock(logs_mutex_);
    return logs_;
}

void HomeVPNCore::clearLogs() {
    std::lock_guard<std::mutex> lock(logs_mutex_);
    logs_.clear();
}

void HomeVPNCore::setStatusCallback(StatusCallback callback) {
    status_callback_ = callback;
}

void HomeVPNCore::setLogCallback(LogCallback callback) {
    log_callback_ = callback;
}

std::string HomeVPNCore::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::localtime(&time_t);
    
    std::stringstream ss;
    ss << std::put_time(&tm, "%H:%M:%S");
    return ss.str();
}

void HomeVPNCore::addLog(const std::string& message) {
    std::lock_guard<std::mutex> lock(logs_mutex_);
    
    std::string timestamped_msg = getCurrentTimestamp() + ": " + message;
    logs_.push_back(timestamped_msg);
    
    // Keep only last 100 log entries
    if (logs_.size() > 100) {
        logs_.erase(logs_.begin());
    }
    
    if (log_callback_) {
        log_callback_(timestamped_msg);
    }
}

std::string HomeVPNCore::executeCommand(const std::string& command) {
    std::string result;
    const std::unique_ptr<FILE, decltype(&pclose)> pipe(popen((command + " 2>&1").c_str(), "r"), pclose);
    
    if (!pipe) {
        addLog("ERROR: Failed to execute command: " + command);
        return "Error: Failed to execute command";
    }
    
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe.get()) != nullptr) {
        result += buffer;
    }
    
    // Remove trailing newline
    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }
    
    if (!result.empty()) {
        addLog("Command output: " + result);
    }
    
    return result;
}

std::string HomeVPNCore::getExternalIP() {
    if (config_.check_ip_url.empty()) return "";
    
    CURL* curl;
    CURLcode res;
    std::string response;
    
    curl = curl_easy_init();
    if (!curl) return "";
    
    curl_easy_setopt(curl, CURLOPT_URL, config_.check_ip_url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res == CURLE_OK) {
        // Clean up response
        if (!response.empty()) {
            response.erase(response.find_last_not_of(" \n\r\t") + 1);
        }
        return response;
    }
    
    return "";
}

bool HomeVPNCore::checkVPNConnection() {
    if (!config_.expected_ip.empty()) {
        return status_.current_ip.find(config_.expected_ip) != std::string::npos;
    } else if (!config_.home_ip_prefix.empty()) {
        return status_.current_ip.find(config_.home_ip_prefix) != std::string::npos;
    } else {
        // Fallback: assume connected if we can get an IP
        return !status_.current_ip.empty() && status_.current_ip != "Error" && status_.current_ip.length() > 5;
    }
}

bool HomeVPNCore::checkShareMount() {
    // Simple check using mountpoint command
    int result = system("mountpoint -q /mnt/homeshare 2>/dev/null");
    return (result == 0);
}

void HomeVPNCore::notifyStatusChange() {
    if (status_callback_) {
        status_callback_(status_);
    }
}

void HomeVPNCore::statusMonitorLoop() {
    while (monitor_running_.load()) {
        updateStatus();
        
        // Wait for the configured interval
        for (int i = 0; i < config_.status_check_interval && monitor_running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

size_t HomeVPNCore::WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

