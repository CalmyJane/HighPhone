#include <WebServer.h>
#include <ESPmDNS.h>
#include <ESP32Servo.h>
#include <map>
#include <Arduino.h>
#include <cmath>
#include <FastLED.h>
#include <functional>
#include <string>
#include <sstream>
#include <algorithm>
#include <Preferences.h>
#include <WiFi.h>
#include <Arduino.h>
#include <string>
#include <sstream>
#include <SD.h>
#include <SPI.h>
#include "AudioFileSourceSD.h"
#include "AudioOutputI2S.h"
#include "AudioGeneratorWAV.h"
#include <FastLED.h>
#include <DNSServer.h>

#include <functional>
#define SD_CS_PIN 5  // Chip Select pin for SD card reader
#define AUDIO_PIN 25 // ESP32 DAC output pin
#define LED_PIN 33


// Configuration class
#include <functional> // Include this to use std::function

//Webconfig
class ConfigParameter {
public:
    enum Type { STRING, FLOAT };

    // Default constructor
    ConfigParameter() : type(FLOAT), floatValue(0.0f) {}

    // Constructor for String parameter
    ConfigParameter(const String& name, const String& value)
        : name(name), type(STRING) {
        stringValue = new String(value);
    }

    // Constructor for Float parameter
    ConfigParameter(const String& name, float value)
        : name(name), type(FLOAT), floatValue(value) {}

    // Copy constructor
    ConfigParameter(const ConfigParameter& other)
        : name(other.name), type(other.type) {
        if (type == STRING) {
            stringValue = new String(*other.stringValue);
        } else {
            floatValue = other.floatValue;
        }
    }

    // Move constructor
    ConfigParameter(ConfigParameter&& other) noexcept
        : name(std::move(other.name)), type(other.type) {
        if (type == STRING) {
            stringValue = other.stringValue;
            other.stringValue = nullptr;
        } else {
            floatValue = other.floatValue;
        }
    }

    // Destructor to properly handle the string memory
    ~ConfigParameter() {
        if (type == STRING && stringValue) {
            delete stringValue;
        }
    }

    // Copy assignment operator
    ConfigParameter& operator=(const ConfigParameter& other) {
        if (this == &other) return *this;
        name = other.name;
        type = other.type;
        if (type == STRING) {
            if (stringValue) {
                *stringValue = *other.stringValue;
            } else {
                stringValue = new String(*other.stringValue);
            }
        } else {
            floatValue = other.floatValue;
        }
        return *this;
    }

    // Move assignment operator
    ConfigParameter& operator=(ConfigParameter&& other) noexcept {
        if (this == &other) return *this;
        name = std::move(other.name);
        type = other.type;
        if (type == STRING) {
            delete stringValue;
            stringValue = other.stringValue;
            other.stringValue = nullptr;
        } else {
            floatValue = other.floatValue;
        }
        return *this;
    }

    // Getter for parameter name
    String getName() const { return name; }

    // Getter for type
    Type getType() const { return type; }

    // Get the String value (only call if type is STRING)
    String getStringValue() const {
        if (type == STRING && stringValue) return *stringValue;
        return "";
    }

    // Get the float value (only call if type is FLOAT)
    float getFloatValue() const {
        if (type == FLOAT) return floatValue;
        return 0.0;
    }

    // Setter for String value
    void setValue(const String& newValue) {
        if (type == STRING) {
            if (stringValue) {
                *stringValue = newValue;
            } else {
                stringValue = new String(newValue);
            }
        }
    }

    // Setter for float value
    void setValue(float newValue) {
        if (type == FLOAT) {
            floatValue = newValue;
        }
    }

private:
    String name;
    Type type;
    union {
        String* stringValue;
        float floatValue;
    };
};

class SDReader {
public:
    struct NumberInfo {
        String filePath;
        String description;
    };

    SDReader() {
        // Empty default constructor
    }

    void initialize() {
        // Initialize SD card
        if (!SD.begin(SD_CS_PIN)) {
            Serial.println("Failed to initialize SD card");
            // Handle error, maybe enter an error state
            return;
        }

        initializeMappings();
    }

    void refreshMappings() {
        initializeMappings();
    }

    const std::map<String, NumberInfo>& getNumberMappings() const {
        return numberMappings;
    }

    bool getNumberInfo(const String& number, NumberInfo& info) const {
        auto it = numberMappings.find(number);
        if (it != numberMappings.end()) {
            info = it->second;
            return true;
        } else {
            return false;
        }
    }

private:
    std::map<String, NumberInfo> numberMappings;

    void initializeMappings() {
        numberMappings.clear(); // Clear existing mappings
        File numbersFolder = SD.open("/numbers");
        if (!numbersFolder || !numbersFolder.isDirectory()) {
            Serial.println("Failed to open /numbers directory");
            return;
        }

        File file = numbersFolder.openNextFile();
        while (file) {
            if (!file.isDirectory()) {
                String fileName = file.name();
                if (fileName.endsWith(".wav")) {
                    // Extract number and description
                    String nameWithoutExt = fileName.substring(0, fileName.length() - 4);
                    int underscoreIndex = nameWithoutExt.indexOf('_');
                    if (underscoreIndex != -1) {
                        String number = nameWithoutExt.substring(0, underscoreIndex);
                        String description = nameWithoutExt.substring(underscoreIndex + 1);
                        String filePath = "/numbers/" + String(fileName);
                        NumberInfo info = { filePath, description };
                        numberMappings[number] = info;
                        Serial.printf("Loaded number: %s, description: %s, file: %s\n", number.c_str(), description.c_str(), filePath.c_str());
                    } else {
                        Serial.printf("Filename format incorrect: %s\n", fileName.c_str());
                    }
                }
            }
            file.close();
            file = numbersFolder.openNextFile();
        }
        numbersFolder.close();
    }
};

class WebConfig {
public:
    WebConfig(const char* ssid, const char* password, SDReader* sdReaderPtr)
        : softAP_ssid(ssid), softAP_password(password), server(80), title("Configuration Page"), sdReader(sdReaderPtr) {}

    void begin() {
        preferences.begin("webconfig", false);  // Open NVS with namespace 'webconfig'
        loadParameters();  // Load parameters from NVS on startup
        configureAccessPoint();
        setupDNS();
        setupWebServer();
    }

    void handleClient() {
        dnsServer.processNextRequest();
        server.handleClient();
    }

    void addParamString(const String& name, const String& defaultValue) {
        String storedValue = defaultValue;
        if (preferences.isKey(name.c_str())) {
            // Load the value from NVS (either default or stored)
            storedValue = preferences.getString(name.c_str(), defaultValue);
        }
        configParams[name] = ConfigParameter(name, storedValue);
        saveParameter(name);
    }

    void addParamFloat(const String& name, float defaultValue) {
        float storedValue = defaultValue;
        if (preferences.isKey(name.c_str())) {
            // Load the value from NVS (either default or stored)
            storedValue = preferences.getFloat(name.c_str(), defaultValue);
        }
        configParams[name] = ConfigParameter(name, storedValue);
        saveParameter(name);
    }

    String getParamString(const String& name) {
        return configParams[name].getStringValue();
    }

    float getParamFloat(const String& name) {
        return configParams[name].getFloatValue();
    }

    void setParam(const String& name, const String& value) {
        if (configParams[name].getType() == ConfigParameter::STRING) {
            configParams[name].setValue(value);
            saveParameter(name);  // Save modified parameter to NVS
        }
    }

    void setParam(const String& name, float value) {
        if (configParams[name].getType() == ConfigParameter::FLOAT) {
            configParams[name].setValue(value);
            saveParameter(name);  // Save modified parameter to NVS
        }
    }

    // Method to set the dynamic title
    void setTitle(const String& newTitle) {
        title = newTitle;
    }

    // Modify this method to accept a callback function
    void setCustomHTML(std::function<String()> callback) {
        getCustomHtmlCallback = callback;
    }

    String getHtmlButton(const String& buttonName, const String& buttonLabel, const String& style = "") {
        return "<button style='" + style + "' onclick=\"window.location.href='/button?name=" + buttonName + "'\">" + buttonLabel + "</button>";
    }

    void onWebButtonPressed(std::function<void(String)> callback) {
        webButtonCallback = callback;
    }

    void onPropertiesModified(std::function<void(void)> callback) {
        propertiesModifiedCallback = callback;
    }

    void onUploadComplete(std::function<void()> callback) {
        uploadCompleteCallback = callback;
    }

private:
    File uploadFile; // To store the file being uploaded
    bool uploadFileAllowed = true; // Flag to allow or reject the upload
    std::function<void()> uploadCompleteCallback; // Callback after upload
    const char* softAP_ssid;
    const char* softAP_password;
    IPAddress apIP = IPAddress(8, 8, 8, 8); // Access Point IP Address
    IPAddress netMsk = IPAddress(255, 255, 255, 0); // Netmask
    const byte DNS_PORT = 53;
    DNSServer dnsServer;
    WebServer server;
    String title;  // Dynamic title for the configuration page
    SDReader* sdReader; // Pointer to SDReader instance

    // Replace customHTML String with a callback function
    std::function<String()> getCustomHtmlCallback;

    std::function<void(String)> webButtonCallback;
    std::function<void(void)> propertiesModifiedCallback; // Callback for property modifications

    Preferences preferences;  // NVS Preferences for storing parameters

    std::map<String, ConfigParameter> configParams; // Configuration storage

    void configureAccessPoint() {
        WiFi.softAPConfig(apIP, apIP, netMsk);
        WiFi.softAP(softAP_ssid, softAP_password);
        delay(1000);
        Serial.print("AP IP address: ");
        Serial.println(WiFi.softAPIP());
    }

    void setupDNS() {
        dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        dnsServer.start(DNS_PORT, "*", apIP);
    }

    void setupWebServer() {
        server.on("/", [this]() { handleRoot(); });
        server.on("/generate_204", [this]() { handleRoot(); }); // Handle Android captive portal request
        server.on("/submit", [this]() { handleSubmit(); });     // Form submission
        server.on("/button", [this]() { handleButton(); });     // Button click handler

        server.on("/upload", HTTP_POST, 
            [this]() { handleUploadComplete(); }, // After the upload is done
            [this]() { handleFileUpload(); } // Handle the upload data
        );

        server.on("/delete", HTTP_GET, [this]() { handleDelete(); });

        server.onNotFound([this]() { handleNotFound(); });
        server.begin();
        Serial.println("HTTP server started");
    }

    // Save parameter to NVS
    void saveParameter(const String& paramName) {
        ConfigParameter& param = configParams[paramName];
        if (param.getType() == ConfigParameter::STRING) {
            preferences.putString(paramName.c_str(), param.getStringValue());
        } else if (param.getType() == ConfigParameter::FLOAT) {
            preferences.putFloat(paramName.c_str(), param.getFloatValue());
        }
    }

    void loadParameters() {
        for (auto& param : configParams) {
            String paramName = param.first;
            if (param.second.getType() == ConfigParameter::STRING) {
                String value = preferences.getString(paramName.c_str(), param.second.getStringValue());
                param.second.setValue(value);
            } else if (param.second.getType() == ConfigParameter::FLOAT) {
                float value = preferences.getFloat(paramName.c_str(), param.second.getFloatValue());
                param.second.setValue(value);
            }
        }
    }

    void handleFileUpload() {
        HTTPUpload& upload = server.upload();

        if(upload.status == UPLOAD_FILE_START){
            String filename = upload.filename;
            Serial.print("Upload File Name: ");
            Serial.println(filename);

            // **Validate File Extension**
            if(!filename.endsWith(".wav")) {
                Serial.println("Only .wav files are allowed");
                uploadFileAllowed = false;
                return;
            }

            // **Ensure the /numbers Directory Exists**
            if(!SD.exists("/numbers")){
                SD.mkdir("/numbers");
            }

            // **Create the File on SD Card**
            String filepath = "/numbers/" + filename;
            uploadFile = SD.open(filepath.c_str(), FILE_WRITE);
            if(uploadFile){
                Serial.print("Uploading to: ");
                Serial.println(filepath);
                uploadFileAllowed = true;
            } else {
                Serial.println("Failed to open file for writing");
                uploadFileAllowed = false;
            }
        }
        else if(upload.status == UPLOAD_FILE_WRITE){
            if(uploadFileAllowed && uploadFile){
                uploadFile.write(upload.buf, upload.currentSize);
            }
        }
        else if(upload.status == UPLOAD_FILE_END){
            if(uploadFileAllowed && uploadFile){
                uploadFile.close();
                Serial.println("File upload complete");
            }
        }
    } 

    void handleUploadComplete(){
        if(uploadFileAllowed){
            Serial.println("Upload Complete. Refreshing number mappings.");
            if(uploadCompleteCallback){
                uploadCompleteCallback(); // Refresh SDReader mappings
            }
            // Redirect to the main page with a success message
            server.sendHeader("Location", "/?upload=success", true);
            server.send(303); // 303 See Other
        } else {
            Serial.println("Upload Failed.");
            // Redirect to the main page with an error message
            server.sendHeader("Location", "/?upload=failed", true);
            server.send(303); // 303 See Other
        }
    }


    void handleButton() {
        if (server.hasArg("name")) {
            String buttonName = server.arg("name");
            // Call the webButtonCallback if it's set
            if (webButtonCallback) {
                webButtonCallback(buttonName);
            }
            // Optionally, redirect back to the main page
            server.send(200, "text/html", "<html><body><script>window.location.href = '/';</script></body></html>");
        } else {
            server.send(400, "text/plain", "Bad Request: Missing 'name' parameter");
        }
    }

    void handleDelete() {
        if (server.hasArg("number")) {
            String number = server.arg("number");
            SDReader::NumberInfo info;
            if (sdReader->getNumberInfo(number, info)) { // Use '->' to access members
                // Attempt to delete the file
                if (SD.remove(info.filePath.c_str())) {
                    Serial.printf("Deleted file: %s\n", info.filePath.c_str());
                    // Refresh the mappings
                    sdReader->refreshMappings();

                    // Redirect back with success message
                    server.sendHeader("Location", "/?delete=success", true);
                    server.send(303); // 303 See Other
                } else {
                    Serial.printf("Failed to delete file: %s\n", info.filePath.c_str());
                    // Redirect back with failure message
                    server.sendHeader("Location", "/?delete=failed", true);
                    server.send(303); // 303 See Other
                }
            } else {
                Serial.printf("Number %s not found for deletion.\n", number.c_str());
                // Redirect back with not found message
                server.sendHeader("Location", "/?delete=notfound", true);
                server.send(303); // 303 See Other
            }
        } else {
            Serial.println("Delete request missing 'number' parameter.");
            // Redirect back with bad request message
            server.sendHeader("Location", "/?delete=badrequest", true);
            server.send(303); // 303 See Other
        }
    }



    void handleRoot() {
        if (captivePortal()) {
            return;
        }

        server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        server.sendHeader("Pragma", "no-cache");
        server.sendHeader("Expires", "-1");

        // Create an HTML page with a dynamic title and tabs for each group
        String p = F("<html><head>"
                    "<style>"
                    "body {"
                    "  margin: 0;"
                    "  font-family: Arial, sans-serif;"
                    "  background-color: #f0f0f0;"
                    "  height: 100vh;"
                    "  overflow-x: hidden;" /* Prevent horizontal scroll */
                    "}"
                    ".header {"
                    "  width: 100%;"
                    "  background-color: #fff;"
                    "  padding: 10px 0;"
                    "  position: sticky;" /* Keep the header at the top when scrolling */ 
                    "  top: 0;"
                    "  z-index: 1000;"
                    "  box-shadow: 0 2px 4px rgba(0,0,0,0.1);"
                    "  text-align: center;"
                    "}"
                    ".header h1 {"
                    "  font-size: 5em;"
                    "  color: #333;"
                    "  margin: 10px 0;"
                    "  -webkit-text-stroke: 3px transparent;"
                    "  text-shadow: 0 0 12px rgba(0, 0, 0, 0.5);"
                    "  animation: textOutlineAnimation 3s infinite ease-in-out;"
                    "}"
                    "@keyframes textOutlineAnimation {"
                    "  0%, 100% { -webkit-text-stroke: 2px transparent; text-shadow: 0 0 6px rgba(0, 0, 0, 0.5); }"
                    "  50% { -webkit-text-stroke: 2px #4CAF50; text-shadow: none; }"
                    "}"
                    ".svg-container {"
                    "  width: 100%;"
                    "  display: flex;"
                    "  justify-content: center;"
                    "  margin-bottom: 10px;"
                    "  padding: 15;"
                    "}"
                    "svg {"
                    "  width: 60%;"
                    "  max-width: 1080px;"
                    "}"
                    ".svg-outline {"
                    "  fill: none;"
                    "  stroke: black;"
                    "  stroke-width: 2;"
                    "  stroke-dasharray: 10, 5;"
                    "  animation: dash 5s linear infinite;"
                    "}"
                    "@keyframes dash {"
                    "  to { stroke-dashoffset: -50; }"
                    "}"
                    ".tab-container {"
                    "  width: 100%;"
                    "  display: flex;"
                    "  justify-content: center;"
                    "  margin-top: 20px;"
                    "}"
                    "ul {"
                    "  list-style-type: none;"
                    "  padding: 0;"
                    "  margin: 0;"
                    "  width: 80%;" /* Full width of the tab container */
                    "  display: flex;"
                    "  justify-content: center;" /* Center the tabs */
                    "  overflow-x: auto;" /* Allow horizontal scrolling for smaller screens */ 
                    "}"
                    "li {"
                    "  flex: 1;"
                    "  text-align: center;"
                    "  margin-right: 10px;"
                    "}"
                    "a {"
                    "  font-size: 2em;"
                    "  text-decoration: none;"
                    "  color: #333;"
                    "  padding: 10px;"
                    "  background-color: #f0f0f0;"
                    "  border: 1px solid #ccc;"
                    "  border-radius: 5px;"
                    "  display: block;"
                    "  width: 100%;"
                    "  box-sizing: border-box;"
                    "}"
                    "a:hover {"
                    "  background-color: #ddd;"
                    "}"
                    ".tab-content {"
                    "  display: none;"
                    "  width: 80%;"
                    "  padding: 0px;"
                    "  margin: 20px auto;"
                    "}"
                    ".active-tab {"
                    "  display: block;"
                    "}"
                    "form {"
                    "  background: white;"
                    "  padding: 20px;"
                    "  border-radius: 10px;"
                    "  box-shadow: 0 4px 8px rgba(0,0,0,0.1);"
                    "  width: 100%;"
                    "  box-sizing: border-box;"
                    "  margin: 0 auto;"
                    "}"
                    "label, input {"
                    "  display: block;"
                    "  width: 100%;"
                    "  margin-bottom: 3px;"
                    "  font-size: 3em;"
                    "  font-weight: bold;"
                    "}"
                    "input {"
                    "  padding: 10px;"
                    "  border: 1px solid #ccc;"
                    "  border-radius: 5px;"
                    "  font-size: 3em;"
                    "  box-sizing: border-box;"
                    "}"
                    "input[type='submit'] {"
                    "  background-color: #333333;"
                    "  color: white;"
                    "  border: none;"
                    "  cursor: pointer;"
                    "  padding: 15px;"
                    "  transition: background-color 0.3s ease;"
                    "  font-size: 3em;"
                    "}"
                    "input[type='submit']:hover {"
                    "  background-color: #45a049;"
                    "}"
                    "</style>"
                    
                    // JavaScript to handle the tab switching
                    "<script>"
                    "function openTab(tabName) {"
                    "  var i, tabcontent;"
                    "  tabcontent = document.getElementsByClassName('tab-content');"
                    "  for (i = 0; i < tabcontent.length; i++) {"
                    "    tabcontent[i].style.display = 'none';"
                    "  }"
                    "  document.getElementById(tabName).style.display = 'block';"
                    "}"
                    "</script>"
                    
                    "</head><body>");

        // Insert SVG animation and title in a fixed header container
        p += "<div class='header'><div class='svg-container'>";
        p += "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 150 126\">";
        p += "<g transform=\"translate(-28.34617, -67.34671)\">";
        p += "<path class=\"svg-outline\" d=\"M46.648479 131.26477v13.51339h-0.003v17.82217H159.91391V144.77816H64.562629V131.26477ZM126.77385 99.98706h18.61959v18.63263h-18.61959zm-62.580031 0h18.61959v18.63263H64.193819ZM28.346749 67.346711c-0.002 41.819719 0.002 84.474009 0 126.000059h0.0486 149.900931V72.846631h0.0501l-0.0501 -5.49992zm5.49992 5.49992H172.79633V187.84685H33.846669Z\" />";
        p += "</g></svg></div>";
        p += "<h1>" + title + "</h1></div>";

        // Check for upload status in URL
        if(server.hasArg("upload")){
            String uploadStatus = server.arg("upload");
            if(uploadStatus == "success"){
                p += "<p style='color: green; font-size: 2rem; text-align: center;'>File uploaded successfully!</p>";
            }
            else if(uploadStatus == "failed"){
                p += "<p style='color: red; font-size: 2rem; text-align: center;'>File upload failed. Only .wav files are allowed.</p>";
            }
        }

        if(server.hasArg("delete")){
            String deleteStatus = server.arg("delete");
            if(deleteStatus == "success"){
                p += "<p style='color: green; font-size: 2rem; text-align: center;'>File deleted successfully!</p>";
            }
            else if(deleteStatus == "failed"){
                p += "<p style='color: red; font-size: 2rem; text-align: center;'>Failed to delete the file.</p>";
            }
            else if(deleteStatus == "notfound"){
                p += "<p style='color: orange; font-size: 2rem; text-align: center;'>File not found.</p>";
            }
            else if(deleteStatus == "badrequest"){
                p += "<p style='color: red; font-size: 2rem; text-align: center;'>Bad delete request.</p>";
            }
        }

        // Collect group names and parameters
        std::map<String, std::vector<String>> groupedParams;
        std::vector<String> noGroupParams;

        // Organize parameters by group or no group
        for (const auto& param : configParams) {
            String paramName = param.first;
            int underscoreIndex = paramName.indexOf('_');
            if (underscoreIndex != -1) {
                // Grouped parameter (group_name_parameter_name)
                String groupName = paramName.substring(0, underscoreIndex);
                String subParamName = paramName.substring(underscoreIndex + 1);
                groupedParams[groupName].push_back(subParamName);
            } else {
                // Parameter without a group
                noGroupParams.push_back(paramName);
            }
        }

        // Display the tabs for each group and the Home tab for non-grouped parameters
        p += "<div class='tab-container'><ul>";
        p += "<li><a onclick=\"openTab('home')\">Home</a></li>";
        for (const auto& group : groupedParams) {
            p += "<li><a onclick=\"openTab('" + group.first + "')\">" + group.first + "</a></li>";
        }
        p += "</ul></div>";

        // Display non-grouped parameters (Home Tab)
        p += "<div id='home' class='tab-content active-tab'><form action=\"/submit\" method=\"POST\">";
        if (!noGroupParams.empty()) {
            for (const String& paramName : noGroupParams) {
                const ConfigParameter& param = configParams[paramName];
                p += "<label for='" + paramName + "'>" + paramName + ":</label>";
                if (param.getType() == ConfigParameter::STRING) {
                    p += "<input type='text' name='" + paramName + "' value='" + param.getStringValue() + "'><br>";
                } else if (param.getType() == ConfigParameter::FLOAT) {
                    p += "<input type='number' step='any' name='" + paramName + "' value='" + String(param.getFloatValue()) + "'><br>";
                }
            }
            p += "<input type='submit' value='Submit'>";
        } else {
            p += "<p>No parameters available on this page.</p>";
        }
        p += "</form></div>";

        // Display grouped parameters (Each group in its own tab)
        for (const auto& group : groupedParams) {
            p += "<div id='" + group.first + "' class='tab-content'><form action=\"/submit\" method=\"POST\">";
            for (const String& subParamName : group.second) {
                String fullParamName = group.first + "_" + subParamName;
                const ConfigParameter& param = configParams[fullParamName];
                p += "<label for='" + fullParamName + "'>" + subParamName + ":</label>";
                if (param.getType() == ConfigParameter::STRING) {
                    p += "<input type='text' name='" + fullParamName + "' value='" + param.getStringValue() + "'><br>";
                } else if (param.getType() == ConfigParameter::FLOAT) {
                    p += "<input type='number' step='any' name='" + fullParamName + "' value='" + String(param.getFloatValue()) + "'><br>";
                }
            }
            p += "<input type='submit' value='Submit'></form></div>";
        }

        // At the end of the generated content, insert the custom HTML
        if (getCustomHtmlCallback) {
            String customHTML = getCustomHtmlCallback(); // Call the callback to get custom HTML
            p += customHTML;
        }

        p += "</body></html>";

        server.send(200, "text/html", p);
    }

    void handleSubmit() {
        for (const auto& param : configParams) {
            if (server.hasArg(param.first)) {
                if (param.second.getType() == ConfigParameter::STRING) {
                    setParam(param.first, server.arg(param.first));
                } else if (param.second.getType() == ConfigParameter::FLOAT) {
                    setParam(param.first, server.arg(param.first).toFloat());
                }
            }
        }

        // Call the propertiesModifiedCallback if it's set
        if (propertiesModifiedCallback) {
            propertiesModifiedCallback();
        }

        // Instead of showing a separate page, reload the current page after submission
        server.send(200, "text/html", "<html><body><script>window.location.href = '/';</script></body></html>");
    }

    void handleNotFound() {
        if (captivePortal()) {
            return;
        }
        String message = "404 Not Found\n\n";
        message += "URI: ";
        message += server.uri();
        message += "\nMethod: ";
        message += (server.method() == HTTP_GET) ? "GET" : "POST";
        message += "\nArguments: ";
        message += server.args();
        message += "\n";
        for (uint8_t i = 0; i < server.args(); i++) {
            message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
        }
        server.send(404, "text/plain", message);
    }

    boolean captivePortal() {
        if (!isIp(server.hostHeader())) {
            Serial.println("Request redirected to captive portal");
            server.sendHeader("Location", String("http://") + toStringIp(server.client().localIP()), true);
            server.send(302, "text/plain", "");
            server.client().stop();
            return true;
        }
        return false;
    }

    bool isIp(String str) {
        for (size_t i = 0; i < str.length(); i++) {
            int c = str.charAt(i);
            if ((c != '.') && (c < '0' || c > '9')) {
                return false;
            }
        }
        return true;
    }

    String toStringIp(IPAddress ip) {
        return String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
    }
};

//Phone specific
class RotaryDial {
  private:
      int pulsePin;
      int rotationPin;
      int pulseCount;
      bool dialing;
      unsigned long lastPulseTime;
      unsigned long lastRotationStateChange;
      unsigned long debounceDelay;
      bool lastRotationState;
      bool lastPulseState;
      int dialedNumber;

  public:
    RotaryDial(int pulsePin, int rotationPin) {
        this->pulsePin = pulsePin;
        this->rotationPin = rotationPin;
        this->pulseCount = 0;
        this->dialing = false;
        this->lastPulseTime = 0;
        this->lastRotationStateChange = 0;
        this->debounceDelay = 80; // debounce delay in milliseconds
        this->dialedNumber = -1;
        pinMode(pulsePin, INPUT_PULLUP);
        pinMode(rotationPin, INPUT_PULLUP);
        // Initialize states to the actual initial state of the pins
        this->lastRotationState = digitalRead(rotationPin);
        this->lastPulseState = digitalRead(pulsePin);
    }

    void update() {
        // Read current states
        bool currentRotationState = digitalRead(rotationPin);
        bool currentPulseState = digitalRead(pulsePin);

        // Handle rotation state change (start/stop dialing)
        if (currentRotationState != lastRotationState) {
            lastRotationStateChange = millis();
            lastRotationState = currentRotationState;

            if (currentRotationState == LOW) {
                // Dialing started
                pulseCount = 0;
                dialing = true;
                dialedNumber = -1;
            } else {
                // Dialing stopped
                dialing = false;
                if (pulseCount > 0) {
                    dialedNumber = (pulseCount == 10) ? 0 : pulseCount;
                }
            }
        }

        // Debounce pulse counting
        if (dialing && (millis() - lastPulseTime) > debounceDelay) {
            if (currentPulseState == LOW && lastPulseState == HIGH) {
                pulseCount++;
                lastPulseTime = millis();
            }
            lastPulseState = currentPulseState;
        }
    }

    int getNumber() {
        if (dialedNumber != -1) {
            int temp = dialedNumber;
            dialedNumber = -1; // Reset after reading
            return temp;
        }
        return -1;
    }
};

class DialController {
  private:
    RotaryDial rotaryDial;
    int phoneHandlePin;
    unsigned long debounceDelay;
    unsigned long lastHandleChangeTime;
    bool lastHandleState;
    bool handlePickedUp;
    unsigned long lastDigitTime;
    unsigned long dialTimeout;
    String numberBuffer;

    std::function<void(bool)> phoneHandleCallback;
    std::function<void(int)> digitCallback;
    std::function<void(String)> dialledCallback;

  public:
    DialController(int pulsePin, int rotationPin, int phoneHandlePin, unsigned long debounceDelay = 100, unsigned long dialTimeout = 3000)
      : rotaryDial(pulsePin, rotationPin), phoneHandlePin(phoneHandlePin), debounceDelay(debounceDelay), dialTimeout(dialTimeout) {
        this->lastHandleChangeTime = 0;
        pinMode(phoneHandlePin, INPUT_PULLUP);
        // Initialize state to the actual initial state of the pin
        this->lastHandleState = digitalRead(phoneHandlePin);
        this->handlePickedUp = false;
        this->lastDigitTime = 0;
    }

    void setPhoneHandleCallback(std::function<void(bool)> callback) {
        phoneHandleCallback = callback;
    }

    void setDigitCallback(std::function<void(int)> callback) {
        digitCallback = callback;
    }

    void setDialledCallback(std::function<void(String)> callback) {
        dialledCallback = callback;
    }

    bool isHandlePickedUp() const {
        return handlePickedUp;
    }

    void update() {
        // Check for handle state change with debounce
        bool currentHandleState = digitalRead(phoneHandlePin);
        if (currentHandleState != lastHandleState && (millis() - lastHandleChangeTime) > debounceDelay) {
            lastHandleChangeTime = millis();
            lastHandleState = currentHandleState;

            if (currentHandleState == LOW) {
                // Handle picked up
                handlePickedUp = true;
                numberBuffer = ""; // Reset the buffer
            } else {
                // Handle placed down
                handlePickedUp = false;
                numberBuffer = ""; // Reset the buffer
            }

            if (phoneHandleCallback) {
                phoneHandleCallback(handlePickedUp);
            }
        }

        if (handlePickedUp) {
            // Update the rotary dial
            rotaryDial.update();
            int digit = rotaryDial.getNumber();
            if (digit != -1) {
                if (digitCallback) {
                    digitCallback(digit);
                }

                // Append the digit to the number buffer
                numberBuffer += String(digit);
                lastDigitTime = millis();
            }

            // Check if number is dialled based on timeout or max digits
            if ((numberBuffer.length() >= 2 && (millis() - lastDigitTime) > dialTimeout) || numberBuffer.length() >= 16) {
                if (dialledCallback) {
                    dialledCallback(numberBuffer);
                    Serial.println(numberBuffer);
                }

                numberBuffer = ""; // Reset after dialled
            }
        }
    }
};

class WavPlayer {
  public:
      WavPlayer() : source(NULL), output(NULL), decoder(NULL), loopEnabled(false) {}

      void begin() {
          source = new AudioFileSourceSD();
          output = new AudioOutputI2S(0, 1);
          decoder = new AudioGeneratorWAV();
      }

      void playAudio(const String &filePath, bool loop = false) {
          loopEnabled = loop;  // Set whether to loop the audio
          currentFilePath = filePath;
          if (decoder->isRunning()) {
              decoder->stop();
          }

          source->close();
          if (source->open(filePath.c_str())) {
              Serial.printf("Playing '%s' from SD card...\n", filePath.c_str());
              decoder->begin(source, output);
          } else {
              Serial.printf("Error opening '%s'\n", filePath.c_str());
          }
      }

      void stop() {
          loopEnabled = false;  // Disable looping
          if (decoder && decoder->isRunning()) {
              decoder->stop();
              Serial.println("Playback stopped.");
          }
      }

      void setVolume(float volume) {
          if (output) {
              output->SetGain(volume / 100); // Set volume (0.0 = mute, 1.0 = max)
              Serial.printf("Volume set to %.2f\n", volume);
          }
      }

    void loop() {
        if (decoder && decoder->isRunning()) {
            if (!decoder->loop()) {
                decoder->stop();
                if (loopEnabled) {
                    // Close and reopen the source file
                    source->close();
                    if (source->open(currentFilePath.c_str())) {
                        // Restart playback if looping is enabled
                        decoder->begin(source, output);
                    } else {
                        Serial.printf("Error reopening '%s'\n", currentFilePath.c_str());
                    }
                }
            }
        }
    }



      bool isPlaying() {
          return decoder && decoder->isRunning();
      }

  private:
      AudioFileSourceSD *source;
      AudioOutputI2S *output;
      AudioGeneratorWAV *decoder;
      bool loopEnabled;  // Track if looping is enabled
        String currentFilePath; // Store the current file path
};

enum PhoneState {
    Idle,
    Dialing,
    Calling,
    InvalidNumber,
    Ringing 
};

class FrontLED {
  public:
      enum Mode { RAINBOW, PULSE, BLINK, CONSTANT, OFF };

      // Constructor
      FrontLED(uint8_t pin, uint8_t numLeds = 1)
          : ledPin(pin), numLeds(numLeds), gHue(0), brightness(0), isIncreasing(true), mode(OFF), color(CRGB::White), rate(20), dutyCycle(128) {
          FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, numLeds);
      }

      // Setters for configuration
      void setMode(Mode newMode) { mode = newMode; }
      void setColor(CRGB newColor) { color = newColor; }
      void setRate(uint16_t newRate) { rate = newRate; }
      void setDutyCycle(uint8_t newDutyCycle) { dutyCycle = newDutyCycle; }

      // Main loop function to update the LED based on the current mode
      void update() {
          unsigned long currentMillis = millis();
          if (currentMillis - previousMillis >= rate) {
              previousMillis = currentMillis;

              switch (mode) {
                  case RAINBOW:
                      updateRainbow();
                      break;
                  case PULSE:
                      updatePulse();
                      break;
                  case BLINK:
                      updateBlink();
                      break;
                  case CONSTANT:
                      updateConstant();
                      break;
                  case OFF:
                      updateOff();
                      break;
              }
              FastLED.show();
          }
      }

  private:
    uint8_t ledPin;
    uint8_t numLeds;
    CRGB leds[1];
    uint8_t gHue;
    uint8_t brightness;
    bool isIncreasing;
    Mode mode;
    CRGB color;
    uint16_t rate;
    uint8_t dutyCycle;
    unsigned long previousMillis = 0;

    void updateRainbow() {
        leds[0] = CHSV(gHue++, 255, 255);
    }

    void updatePulse() {
        if (isIncreasing) {
            brightness += 2;
            if (brightness >= dutyCycle) isIncreasing = false;
        } else {
            brightness -= 2;
            if (brightness <= 0) isIncreasing = true;
        }
        leds[0] = color;
        leds[0].fadeLightBy(255 - brightness);
    }

    void updateBlink() {
        static bool isOn = false;
        if (isOn) {
            leds[0] = color;
        } else {
            leds[0] = CRGB::Black;
        }
        isOn = !isOn;
    }

    void updateConstant() {
        leds[0] = color;
    }

    void updateOff() {
        leds[0] = CRGB::Black;
    }
};

class PhoneController {
  private:
    SDReader* sdReader; 
    WavPlayer* wavPlayer;
    DialController dialController;
    PhoneState currentState;
    PhoneState lastState;
    String lastNumber = "";
    String incomingNumber = "";
    unsigned long ringStartTime;
    unsigned long ringDuration;
    unsigned long ringVariation;
    unsigned long actualRingDuration;

    std::function<void(PhoneState, PhoneState)> stateChangeCallback;

    void transitionToState(PhoneState newState) {
        if (currentState != newState) {
            lastState = currentState;
            currentState = newState;

            if (currentState == Ringing) {
                // Start the ringing timer
                ringStartTime = millis();

                // Compute the actual ringing duration with random variation
                if (ringVariation > 0) {
                    long variation = random(- (long)ringVariation, (long)ringVariation + 1);
                    actualRingDuration = ringDuration + variation;
                } else {
                    actualRingDuration = ringDuration;
                }
                if (actualRingDuration < 0) actualRingDuration = 0;
            }

            if (stateChangeCallback) {
                stateChangeCallback(lastState, currentState);
            }
        }
    }


    void onPhoneHandleChange(bool pickedUp) {
        if (pickedUp) {
            if (currentState == Idle) {
                transitionToState(Dialing);
            } else if (currentState == Ringing) {
                // Incoming call is being answered
                transitionToState(Calling);
            }
        } else {
            if (currentState == Calling || currentState == InvalidNumber || currentState == Ringing) {
                // Stop the WAV file if still playing
                wavPlayer->stop();
            }
            transitionToState(Idle);
        }
    }

    void onDigitDialled(int digit) {
        if (currentState == Dialing) {
            // Proceed with digit processing only if the current state is Dialing
            transitionToState(Dialing);
        }
    }

    void onNumberDialled(String number) {
        if (currentState == Dialing) {
            lastNumber = number;
            if (isValidNumber(number)) {
                transitionToState(Calling);
            } else {
                transitionToState(InvalidNumber);
            }
        }
    }

  public:
    PhoneController(int pulsePin, int rotationPin, int phoneHandlePin, SDReader* sdReader, WavPlayer* wavPlayer)
        : sdReader(sdReader), wavPlayer(wavPlayer), dialController(pulsePin, rotationPin, phoneHandlePin) {
        currentState = Idle;
        lastState = Idle;
        stateChangeCallback = nullptr;

        // Set the callbacks using lambdas that capture 'this'
        // Ensure these do not access sdReader or wavPlayer in a way that requires SD card access
        dialController.setPhoneHandleCallback([this](bool pickedUp) { this->onPhoneHandleChange(pickedUp); });
        dialController.setDigitCallback([this](int digit) { this->onDigitDialled(digit); });
        dialController.setDialledCallback([this](String number) { this->onNumberDialled(number); });
    }


    void setStateChangeCallback(std::function<void(PhoneState, PhoneState)> callback) {
        stateChangeCallback = callback;
    }

    void startCall(String number) {
        if (currentState == Idle && !dialController.isHandlePickedUp()) {
            // Incoming call can only start when the phone is idle and handle is down
            incomingNumber = number; // Store the incoming number
            lastNumber = number;
            transitionToState(Ringing);
        }
    }

    void stopCall(){
        wavPlayer->stop();
        transitionToState(Idle);
    }

    bool isValidNumber(const String& number) {
        SDReader::NumberInfo info;
        return sdReader->getNumberInfo(number, info);
    }

    String getCurrentNumber() const {
        return lastNumber;
    }

    void setRingDuration(unsigned long duration) {
        ringDuration = duration;
    }

    void setRingVariation(unsigned long variation) {
        ringVariation = variation;
    }

    PhoneState getCurrentState(){
        return currentState;
    }

    void dialNumber(String number){
        onNumberDialled(number);
    }

    void update() {
        dialController.update();

        // Check if the call is in the 'Calling' state and the WAV file has finished
        if (currentState == Calling && !wavPlayer->isPlaying()) {
            transitionToState(Idle);  // Automatically transition to Idle after playback
        }

        // Check for Ringing state timeout
        if (currentState == Ringing) {
            unsigned long elapsedTime = millis() - ringStartTime;
            if (elapsedTime >= actualRingDuration) {
                transitionToState(Idle); // Transition back to Idle after ringing duration
            }
        }

        // Also check if the InvalidNumber state is ongoing, and loop the audio if necessary
        if (currentState == InvalidNumber) {
            wavPlayer->loop();  // Ensure the message keeps playing
        }
    }

};

class ButtonHandler {
public:
    // Method to add a button with a name, pin number, and an optional inversion flag
    void addButton(const String& name, uint8_t pin, bool inverted = false) {
        // Configure the pin as input with internal pull-up
        pinMode(pin, INPUT_PULLUP);
        ButtonState state;
        state.pin = pin;
        state.inverted = inverted;
        state.lastStableState = readButton(pin, inverted);
        state.lastReading = state.lastStableState;
        state.lastDebounceTime = millis();
        buttons[name] = state;
    }

    // Method to set the button state changed callback
    void onButtonStateChanged(std::function<void(String, bool)> callback) {
        buttonCallback = callback;
    }

    // Method to update the button states; should be called in the loop()
    void update() {
        unsigned long currentTime = millis();
        for (auto& pair : buttons) {
            const String& name = pair.first;
            ButtonState& state = pair.second;
            bool reading = readButton(state.pin, state.inverted);

            if (reading != state.lastReading) {
                // reset the debouncing timer
                state.lastDebounceTime = currentTime;
            }

            if ((currentTime - state.lastDebounceTime) > debounceDelay) {
                // whatever the reading is at, it's been there for longer than the debounce delay
                // so take it as the actual current state

                if (reading != state.lastStableState) {
                    state.lastStableState = reading;

                    // Button state changed, call the callback
                    if (buttonCallback) {
                        buttonCallback(name, reading); // Pass the name and the new state (pressed/released)
                    }
                }
            }

            state.lastReading = reading;
        }
    }

    // Method to get the current state of a button
    bool isButtonPressed(const String& name) {
        if (buttons.find(name) != buttons.end()) {
            return buttons[name].lastStableState;
        }
        return false; // Button not found
    }

private:
    struct ButtonState {
        uint8_t pin;
        bool inverted;         // Indicates if the button's logic is inverted
        bool lastStableState;  // The last stable state
        bool lastReading;      // The last reading from the pin
        unsigned long lastDebounceTime;
    };
    
    std::map<String, ButtonState> buttons;
    std::function<void(String, bool)> buttonCallback;
    const unsigned long debounceDelay = 50; // Debounce delay in milliseconds

    // Helper method to read the button state, taking inversion into account
    bool readButton(uint8_t pin, bool inverted) {
        bool state = digitalRead(pin) == LOW;  // Buttons are active LOW due to pull-up
        return inverted ? !state : state;      // Apply inversion if needed
    }
};

// Define an enum to track the speaker mode
enum SpeakerMode {
    Silent,
    Normal,
    Speaker
};

SpeakerMode currentSpeakerMode = Normal; // Initialize to Normal mode by default

SDReader sdReader;  // assuming CS pin is 10
WebConfig webConfig("CJ_HP", "High1234", &sdReader);

WavPlayer wavPlayer;
PhoneController phoneController(22, 21, 15, &sdReader, &wavPlayer); // Passing wavPlayer to PhoneController

// Initialize FrontLED on pin 13
FrontLED frontLED(13);
ButtonHandler buttonHandler;

String generateCustomHtml() {
    String html = "<div class='custom-html'>";

    // Scoped CSS for custom-html container
    html += "<style>"
            /* Reset default margins and paddings within custom-html */
            ".custom-html {"
            "  padding: 20px;" /* Increased padding for larger container */
            "  width: 100%;"
            "  box-sizing: border-box;"
            "  font-family: Arial, sans-serif;"
            "}"
            /* Title Styling */
            ".custom-html h1 {"
            "  text-align: center;"
            "  margin: 30px 0;" /* Increased margins */
            "  font-size: 2rem;" /* Increased font size */
            "  word-wrap: break-word;"
            "}"
            ".custom-html h2 {"
            "  text-align: center;"
            "  margin: 20px 0 10px;" /* Increased margins */
            "  font-size: 1.5rem;" /* Increased font size */
            "}"
            /* Cancel Call Button Styling */
            ".custom-html .cancel-button {"
            "  display: flex;"
            "  align-items: center;"
            "  justify-content: center;"
            "  gap: 20px;" /* Increased gap for better spacing */
            "  width: 90%;" /* Increased width */
            "  max-width: 80vw;" /* Increased max-width for larger screens */
            "  height: 80px;" /* Increased height */
            "  margin: 0 auto 30px auto;" /* Increased bottom margin */
            "  padding: 15px 30px;" /* Increased padding */
            "  background-color: #ff4d4d;"
            "  color: white;"
            "  border: none;"
            "  border-radius: 15px;" /* Increased border-radius for a more pronounced curve */
            "  cursor: pointer;"
            "  font-size: 2.5rem;" /* Increased font size */
            "  font-weight: bold;" /* Added font weight for prominence */
            "  box-shadow: 0 4px 10px rgba(0,0,0,0.2);" /* Added shadow for depth */
            "  transition: background-color 0.3s, transform 0.2s;" /* Added transition for smooth effects */
            "}"
            ".custom-html .cancel-button:hover {"
            "  background-color: #e60000;" /* Darker red on hover */
            "  transform: translateY(-2px);" /* Slight lift on hover */
            "}"
            ".custom-html .cancel-button:active {"
            "  transform: translateY(0px) scale(0.98);" /* Slight shrink on click */
            "}"
            /* Adjust SVG Icon Styling within Cancel Button */
            ".custom-html .cancel-button svg {"
            "  width: 40px;" /* Increased SVG width */
            "  height: 40px;" /* Increased SVG height */
            "}"
            /* Numbers List Styling */
            ".custom-html ul {"
            "  list-style: none;"
            "  padding: 0;"
            "  margin: 0;"
            "  width: 100%;"
            "  display: block;"
            "}"
            ".custom-html li {"
            "  background-color: #ffffff;"
            "  padding: 20px;" /* Increased padding */
            "  margin-bottom: 20px;" /* Increased margin */
            "  border-radius: 10px;" /* Increased border-radius */
            "  box-shadow: 0 4px 10px rgba(0,0,0,0.1);" /* Increased shadow */
            "  display: flex;"
            "  justify-content: space-between;"
            "  align-items: center;"
            "  box-sizing: border-box;"
            "  width: 100%;"
            "}"
            /* Number and Description Styling */
            ".custom-html .number-info {"
            "  display: flex;"
            "  align-items: center;"
            "  font-size: 1.8rem;" /* Increased font size */
            "}"

            ".custom-html .number {"
            "  font-weight: bold;"
            "  margin-right: 15px;" /* Increased margin */
            "}"
            ".custom-html .description {"
            "  font-weight: normal;"
            "  text-align: right;"
            "  font-size: 1.8rem;" /* Increased font size */
            "  max-width: 60vw;" /* Increased max-width */
            "  overflow: hidden;"
            "  word-wrap: break-word;"
            "}"
            /* Buttons Container Styling */
            ".custom-html .buttons {"
            "  display: flex;"
            "  gap: 15px;" /* Increased gap */
            "  align-items: center;"
            "}"
            /* Icon Buttons Styling */
            ".custom-html .icon-button {"
            "  width: 60px;" /* Increased size for better visibility */
            "  height: 60px;" /* Increased size for better visibility */
            "  background-color: #4CAF50;" /* Green for Call */
            "  border: none;"
            "  border-radius: 50%;"
            "  cursor: pointer;"
            "  display: flex;"
            "  align-items: center;"
            "  justify-content: center;"
            "  transition: background-color 0.3s, transform 0.2s;" /* Added transition */
            "}"
            ".custom-html .icon-button.delete {"
            "  background-color: #f44336;" /* Red for Delete */
            "}"
            ".custom-html .icon-button:hover {"
            "  opacity: 0.9;"
            "  transform: scale(1.05);" /* Slight enlarge on hover */
            "}"
            ".custom-html .icon-button:active {"
            "  transform: scale(0.95);" /* Slight shrink on click */
            "}"
            /* Upload Section Styling */
            ".custom-html .upload-section {"
            "  margin: 20px 0 0 0;" /* Increased margin */
            "  padding: 20px;" /* Increased padding */
            "  border: 2px solid #ccc;"
            "  border-radius: 8px;" /* Increased border-radius */
            "  background-color: #ffffff;"
            "  box-sizing: border-box;"
            "  width: 100%;"
            "}"
            ".custom-html .upload-section label {"
            "  font-size: 1.5rem;" /* Increased font size */
            "  display: block;"
            "  margin-bottom: 15px;" /* Increased margin */
            "}"
            ".custom-html .upload-section input[type='file'] {"
            "  font-size: 1.5rem;" /* Increased font size */
            "  padding: 12px;" /* Increased padding */
            "  width: 100%;"
            "  margin-bottom: 15px;" /* Increased margin */
            "  box-sizing: border-box;"
            "}"
            ".custom-html .upload-section input[type='submit'] {"
            "  width: 100%;"
            "  padding: 15px;" /* Increased padding */
            "  background-color: #4CAF50;"
            "  color: white;"
            "  border: none;"
            "  border-radius: 8px;" /* Increased border-radius */
            "  cursor: pointer;"
            "  font-size: 1.5rem;" /* Increased font size */
            "  font-weight: bold;" /* Added font weight */
            "  transition: background-color 0.3s, transform 0.2s;" /* Added transition */
            "}"
            ".custom-html .upload-section input[type='submit']:hover {"
            "  background-color: #45a049;" /* Darker green on hover */
            "  transform: translateY(-2px);" /* Slight lift on hover */
            "}"
            ".custom-html .upload-section input[type='submit']:active {"
            "  transform: translateY(0px) scale(0.98);" /* Slight shrink on click */
            "}"
            "</style>";

    // Title Section
    html += "<h1>" + webConfig.getParamString("title") + "</h1>";

    // Cancel Call Button with Enhanced SVG Icon and Proper Alignment
    html += "<button class='cancel-button' onclick=\"window.location.href='/button?name=cancel_call'\" aria-label='Cancel Call'>";
    // Enhanced SVG Icon for Cancel (a more stylish cross inside a circle)
    html += "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>";
    html += "<circle cx='12' cy='12' r='10' fill='rgba(255, 255, 255, 0.3)'/>";
    html += "<line x1='8' y1='8' x2='16' y2='16'/>";
    html += "<line x1='16' y1='8' x2='8' y2='16'/>";
    html += "</svg>";
    // Button Text
    html += "<span>Cancel Call</span>";
    html += "</button>";

    // Numbers List Section
    html += "<h2>Numbers</h2>";
    html += "<ul>";

    const std::map<String, SDReader::NumberInfo>& mappings = sdReader.getNumberMappings();
    for (const auto& pair : mappings) {
        String number = pair.first;
        String description = pair.second.description;

        html += "<li>";

        // Number and Description
        html += "<div class='number-info'>";
        html += "<span class='number'>" + number + "</span>";
        html += "</div>";

        // Buttons Container
        html += "<div class='buttons'>";
        html += "<span class='description'>" + description + "</span>";

        // Call Button with SVG Icon
        html += "<button class='icon-button call' onclick=\"window.location.href='/button?name=" + number + "'\" aria-label='Call " + number + "'>";
        html += "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='currentColor' stroke='none'>";
        html += "<path d='M6.62 10.79a15.053 15.053 0 006.59 6.59l2.2-2.2a1 1 0 011.11-.21 11.72 11.72 0 003.68.59 1 1 0 011 1v3.5a1 1 0 01-1 1A16 16 0 012 5a1 1 0 011-1h3.5a1 1 0 011 1 11.72 11.72 0 00.59 3.68 1 1 0 01-.21 1.11l-2.2 2.2z'/>";
        html += "</svg>";
        html += "</button>";

        // Delete Button with SVG Icon
        html += "<button class='icon-button delete' onclick=\"confirmDelete('" + number + "')\" aria-label='Delete " + number + "'>";
        html += "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='currentColor' stroke='none'>";
        html += "<path d='M3 6h18v2H3V6zm2 3h14v13a2 2 0 01-2 2H7a2 2 0 01-2-2V9zm5 3v7h2v-7H10zm4 0v7h2v-7h-2z'/>";
        html += "</svg>";
        html += "</button>";

        html += "</div>"; // End Buttons Container
        html += "</li>";
    }
    html += "</ul>";

    // JavaScript for Delete Confirmation
    html += "<script>"
            "function confirmDelete(number) {"
            "  if (confirm('Are you sure you want to delete ' + number + '?')) {"
            "    window.location.href = '/delete?number=' + number;"
            "  }"
            "}"
            "</script>";

    // Upload WAV Files Section
    html += "<h2>Upload WAV Files</h2>";
    html += "<div class='upload-section'>";
    html += "<form action='/upload' method='POST' enctype='multipart/form-data'>";
    html += "<label for='file'>Select WAV File:</label>";
    html += "<input type='file' name='file' accept='.wav' required>";
    html += "<input type='submit' value='Upload WAV File'>";
    html += "</form>";
    html += "</div>";

    html += "</div>"; // End custom-html container
    return html;
}



void applyCurrentVolume() {
    // Get the volume settings from WebConfig
    float volumeNormal = webConfig.getParamFloat("volumes_normal");
    float volumeSilent = webConfig.getParamFloat("volumes_silent");
    float volumeSpeaker = webConfig.getParamFloat("volumes_speaker");

    // Apply the volume based on the current speaker mode
    if (currentSpeakerMode == Silent) {
        wavPlayer.setVolume(volumeSilent);
        Serial.printf("Volume set to Silent mode: %.2f\n", volumeSilent);
    } else if (currentSpeakerMode == Normal) {
        wavPlayer.setVolume(volumeNormal);
        Serial.printf("Volume set to Normal mode: %.2f\n", volumeNormal);
    } else if (currentSpeakerMode == Speaker) {
        wavPlayer.setVolume(volumeSpeaker);
        Serial.printf("Volume set to Speaker mode: %.2f\n", volumeSpeaker);
    }
}

//Button pressed on website
void handleWebButton(String buttonName) {
    if(buttonName == "cancel_call"){
        phoneController.stopCall();
    } else {
        phoneController.startCall(buttonName);
    }
}

//Properties modified through web
void onPropertiesModified() {
    // Handle properties modification if needed
    Serial.println("Properties were modified via the web interface.");

    // Retrieve the ring duration and variation from the web config
    float ringDuration = webConfig.getParamFloat("ringDuration");
    float ringVariation = webConfig.getParamFloat("ringVariation");

    // Update the PhoneController with the new values
    phoneController.setRingDuration((unsigned long)ringDuration);
    phoneController.setRingVariation((unsigned long)ringVariation);
    
    // Apply the current volume based on the speaker mode
    applyCurrentVolume();
}

void updateLEDAnimation(PhoneState state) {
    switch (state) {
        case PhoneState::Idle:
            frontLED.setMode(FrontLED::RAINBOW);
            frontLED.setRate(30);        // Adjust the speed
            frontLED.setDutyCycle(128);  // Adjust the duty cycle
            break;

        case PhoneState::Dialing:
            frontLED.setMode(FrontLED::PULSE);
            frontLED.setColor(CRGB::Yellow);
            frontLED.setRate(10);        // Adjust the speed
            frontLED.setDutyCycle(128);  // Adjust the duty cycle
            break;

        case PhoneState::Calling:
            frontLED.setMode(FrontLED::PULSE);
            frontLED.setColor(CRGB::Green);
            frontLED.setRate(30);        // Adjust the speed
            break;

        case PhoneState::InvalidNumber:
            frontLED.setMode(FrontLED::BLINK);
            frontLED.setColor(CRGB::Red);
            frontLED.setRate(500);         // Blinking rate
            break;

        case PhoneState::Ringing:
            frontLED.setMode(FrontLED::BLINK);
            frontLED.setColor(CRGB::White);
            frontLED.setRate(250);         // Blinking rate
            break;

        default:
            frontLED.setMode(FrontLED::OFF);
            break;
    }
}

//Phone State Change
void onStateChange(PhoneState lastState, PhoneState newState) {
    updateLEDAnimation(newState);  // Update the LED animation based on the new state

    // Retrieve volumes from the WebConfig
    float ringVolume = webConfig.getParamFloat("volumes_speaker");

    if (newState == PhoneState::Calling) {
        wavPlayer.stop();
        String dialledNumber = phoneController.getCurrentNumber();
        SDReader::NumberInfo info;
        if (sdReader.getNumberInfo(dialledNumber, info)) {
            // Set the normal volume for the call
            applyCurrentVolume(); //reset volume to currently selected
            wavPlayer.playAudio(info.filePath);
        } else {
            Serial.println("Error: Number info not found");
        }
    } else if (newState == PhoneState::InvalidNumber) {
        // Invalid number, play the notfound.wav in a loop
        applyCurrentVolume();
        wavPlayer.playAudio("/system/keinAnschluss.wav", true);
    } else if (newState == PhoneState::Idle) {
        wavPlayer.stop();
    } else if (newState == PhoneState::Ringing) {
        // Use ring volume for the ringing state
        wavPlayer.setVolume(ringVolume);  // Set the volume to ring volume when ringing
        wavPlayer.playAudio("/system/ring.wav", true);
    }

    Serial.print("State changed from ");
    Serial.print(lastState);
    Serial.print(" to ");
    Serial.println(newState);
}

//Phone front buttons
void onButtonStateChanged(String name, bool pressed) {
    if (pressed) {  // Only take action when the button is pressed, not released
        if (name == "Speaker") {
            Serial.println("Speaker button pressed.");

            // Cycle through the speaker modes: Silent -> Normal -> Speaker
            if (currentSpeakerMode == Silent) {
                currentSpeakerMode = Normal;
                Serial.println("Switched to Normal mode.");
            } else if (currentSpeakerMode == Normal) {
                currentSpeakerMode = Speaker;
                Serial.println("Switched to Speaker mode.");
            } else {
                currentSpeakerMode = Silent;
                Serial.println("Switched to Silent mode.");
            }

            // Apply the appropriate volume for the new mode
            applyCurrentVolume();
        }
        // Rest of the buttons (Redial and Random) are handled as before...
        else if (name == "Redial") {
            // Redial button logic
            if (phoneController.getCurrentState() == PhoneState::Dialing) {
                Serial.println("Redial button pressed.");
                String lastDialedNumber = phoneController.getCurrentNumber();
                phoneController.dialNumber(lastDialedNumber);
                } else {
                    Serial.println("No number to redial.");
                }
            }
        else if (name == "Random") {
            // Random button logic
            if (phoneController.getCurrentState() == PhoneState::Idle) {
                Serial.println("Random button pressed.");
                const auto& mappings = sdReader.getNumberMappings();
                if (!mappings.empty()) {
                    auto randomIndex = random(0, mappings.size());
                    auto it = std::next(mappings.begin(), randomIndex);
                    String randomNumber = it->first;
                    Serial.print("Dialing random number: ");
                    Serial.println(randomNumber);
                    phoneController.startCall(randomNumber);
                } else {
                    Serial.println("No numbers available for random dialing.");
                }
            }
        }
    }
}


// MAIN
void setup() {
    Serial.begin(115200);
    delay(1000);
    // Seed the random number generator
    randomSeed(analogRead(0));

    sdReader.initialize();

    // Initialize the web configuration
    webConfig.begin();

    webConfig.addParamFloat("volumes_normal", 50);
    webConfig.addParamFloat("volumes_silent", 20);
    webConfig.addParamFloat("volumes_speaker", 100);
    webConfig.addParamFloat("ringDuration", 5000);  // Default duration in milliseconds
    webConfig.addParamFloat("ringVariation", 2000); // Default variation in milliseconds

    // **Set the Upload Complete Callback**
    webConfig.onUploadComplete([](){
        sdReader.refreshMappings(); // Refresh number mappings
        Serial.println("Number mappings refreshed after file upload.");
    });

    // Set the dynamic title
    webConfig.setTitle("HighPhone");
    // Set the custom HTML callback
    webConfig.setCustomHTML(generateCustomHtml);
    // Set the web button callback
    webConfig.onWebButtonPressed(handleWebButton);
    // Set the properties modified callback
    webConfig.onPropertiesModified(onPropertiesModified);

    onPropertiesModified();

    buttonHandler.onButtonStateChanged(onButtonStateChanged);
    buttonHandler.addButton("Speaker", 16);
    buttonHandler.addButton("Redial", 17);
    buttonHandler.addButton("Random", 5, true);

    // Initialize wavPlayer after SD card is ready
    wavPlayer.begin();
    wavPlayer.setVolume(50);
    wavPlayer.playAudio("/system/short_ring.wav");

    updateLEDAnimation(PhoneState::Idle);  // Initialize LED animation

    // Pass sdReader to PhoneController
    phoneController.setStateChangeCallback(onStateChange);
}

void loop() {
    // Continuously update the LED state
    buttonHandler.update();
    webConfig.handleClient();
    frontLED.update();
    phoneController.update();

    wavPlayer.loop(); // Call this in loop if you want continuous playback updates
}

