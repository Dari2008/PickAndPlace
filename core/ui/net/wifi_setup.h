/**
 * wifi_setup.h - network status / WiFi choice on the Pi (same API as the ESP32 version)
 * -----------------------------------------------------------------------------
 * Raspberry Pi OS (Bookworm) manages networks with NetworkManager: this class runs
 * nmcli in a background thread (scan, connect, forget) and reads the address.
 * Ethernet counts as "connected" too. WPS is not offered by NetworkManager:
 * startWps() only reports that.
 *
 * The website password and the login secret are kept in <data>/web.txt, which
 * pap-web (../../../web) reads as well:
 *   password = abcdefgh
 *   secret   = 32 hex chars
 * -----------------------------------------------------------------------------
 */
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

struct WifiInfo
{
    std::string state;  // connecting, connected, wps, wps_failed, off
    std::string ssid;   // active WiFi network ("Ethernet" when wired)
    std::string ip;
    int rssi = 0;
    std::string message;
    std::string pending; // network being tried (connectTo), "" = none
};

struct WifiNet
{
    std::string ssid;
    int rssi = 0;
    bool secure = true;
};

class WifiSetup
{
public:
    void begin(bool simulate);
    void update(); // from the UI loop

    void startWps();
    void forget();

    void startScan();
    bool scanning() const { return scanning_; }
    bool scanFailed() const { return scanFailed_; }
    const std::vector<WifiNet> &networks(); // UI thread only

    void connectTo(const std::string &ssid, const std::string &pass);

    WifiInfo info();
    std::string webPassword();
    void setWebPassword(const std::string &pass);
    std::string newWebPassword();
    std::string secret();

private:
    void poll();
    void loadWeb();
    void saveWeb();

    bool sim_ = false;
    std::mutex m_;
    WifiInfo info_;
    std::vector<WifiNet> found_, networks_;
    std::atomic<bool> scanning_{false}, scanFailed_{false}, busy_{false};
    uint32_t lastPoll_ = 0;
    std::string webPass_, secret_;
    bool webLoaded_ = false;
};

extern WifiSetup wifiSetup;
