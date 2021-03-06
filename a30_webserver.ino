/*******************************************************************************
#                                                                              #
#     An alternative firmware for Xiaomi Desk Lamp (Yeelight)                  #
#                                                                              #
#                                                                              #
#      Copyright (C) 2018 Tom Stöveken                                         #
#                                                                              #
# This program is free software; you can redistribute it and/or modify         #
# it under the terms of the GNU General Public License as published by         #
# the Free Software Foundation; version 2 of the License.                      #
#                                                                              #
# This program is distributed in the hope that it will be useful,              #
# but WITHOUT ANY WARRANTY; without even the implied warranty of               #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                #
# GNU General Public License for more details.                                 #
#                                                                              #
# You should have received a copy of the GNU General Public License            #
# along with this program; if not, write to the Free Software                  #
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA    #
#                                                                              #
********************************************************************************/

#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <FS.h>

ESP8266WebServer server(80);
File fsUploadFile;

ESP8266HTTPUpdateServer httpUpdater;


/******************************************************************************
Description.: Return MIME type based on file extension
Input Value.: filename with extension as String
Return Value: return the MIME type as String or text/plain as default type
******************************************************************************/
String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  else if(filename.endsWith(".manifest")) return "text/cache-manifest";
  else if(filename.endsWith(".json")) return "text/json";
  return "text/plain";
}

/******************************************************************************
Description.: Sends file to client
Input Value.: full path to file or /, then the path will be extended with
              index.htm
Return Value: true if file was send, false no file found
******************************************************************************/
bool handleFileRead(String path){
  if(path.endsWith("/")) path += "index.htm";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

/******************************************************************************
Description.: handles a request to path "/color"
              accepts HTTP-GET parameters for color, sets state accordingly
Input Value.: -
Return Value: -
******************************************************************************/
void handleColorGET() {
  StaticJsonBuffer<500> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  String json;

  if(server.hasArg("ratio") && server.hasArg("brightness")) {
    Log("received new color values, setting state and colors (ratio:"+
      server.arg("ratio") + ", brightness:" +
      server.arg("brightness") + ")");
    state = CONSTANTCOLOR;
    g_ratio = server.arg("ratio").toFloat();
    g_brightness = server.arg("brightness").toFloat();
  }

  root["ratio"] = g_ratio;
  root["brightness"] = g_brightness;

  uint8_t warmwhite, coldwhite;

  root["ww"] = 255 * g_ratio * g_brightness;
  root["cw"] = 255 * (1-g_ratio) * g_brightness;

  root.printTo(json);
  server.send(200, "text/json", json);
}

/******************************************************************************
Description.: handles uploaded files
              this is needed to handle new files to be stored in the SPIFFS
              partition of the flash.
              A convenient way to update the HTML and config files is using
              a BASH file which is part of the project
Input Value.: -
Return Value: -
******************************************************************************/
void handleFileUpload() {
  if ( !enableUpdates ) {
    server.send(200, "text/plain", "locked");
    return;
  }
  
  if(server.uri() != "/edit") return;
  
  HTTPUpload& upload = server.upload();
  
  if(upload.status == UPLOAD_FILE_START){
    String filename = upload.filename;
    
    if( !filename.startsWith("/") )
      filename = "/"+filename;
   
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if(upload.status == UPLOAD_FILE_WRITE) {
    if(fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if(upload.status == UPLOAD_FILE_END) {
    if(fsUploadFile)
      fsUploadFile.close();
  }
}

/******************************************************************************
Description.: handle request to "/all", answers with a JSON encoded details of
              all states, important variables etc at once.
Input Value.: -
Return Value: -
******************************************************************************/
void handleAllGET() {
  StaticJsonBuffer<500> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  String json;

  // translate current state to string
  for(int i=0; i<LENGTH_OF(state_map); i++) {
    if(state_map[i].state == state) {
      root["state"] = state_map[i].state_as_string;
      root["state_human_readable"] = state_map[i].human_readable_string;
      break;
    }
  }
  
  root["ratio"] = g_ratio;
  root["brightness"] = g_brightness;

  root["uptime"] = millis();
  root["heap"] = ESP.getFreeHeap();
  root["RSSI"] = WiFi.RSSI();

  root.printTo(json);
  server.send(200, "text/json", json);  
}

/******************************************************************************
Description.: handles requests to "/config.json"
              if HTTP-GET parameters set new values, those are stored
              it sends the contents of the file "config.json" to the HTTP-client
Input Value.: -
Return Value: -
******************************************************************************/
void handleConfigGET() {
  StaticJsonBuffer<500> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();

  if(server.hasArg("hostname") &&
     server.hasArg("startupcolor_warmwhite") &&
     server.hasArg("startupcolor_coldwhite") ) {

    Log("a new config was submitted");

    root["hostname"] = server.arg("hostname");
    root["startupcolor_coldwhite"] = server.arg("startupcolor_coldwhite").toInt();
    root["startupcolor_warmwhite"] = server.arg("startupcolor_warmwhite").toInt();
    root["send_WLAN_keep_alive_packet"] = g_send_WLAN_keep_alive_packet;

    // store to file
    File fsConfig = SPIFFS.open("/config.json", "w");
    if (fsConfig) {
      root.printTo(fsConfig);
      fsConfig.close();
    }
  }

  if(!handleFileRead("/config.json"))
    server.send(404, "text/plain", "FileNotFound");
}

/******************************************************************************
Description.: prepares the webserver, sets up the special paths and their
              handler-functions
Input Value.: -
Return Value: -
******************************************************************************/
void setup_webserver() {
  //called when the url is not defined here
  //use it to load content from SPIFFS
  server.onNotFound([](){
    if(!handleFileRead(server.uri()))
      server.send(404, "text/plain", "FileNotFound");
  });

  server.on("/all", HTTP_GET, handleAllGET);
  server.on("/color", HTTP_GET, handleColorGET);
  server.on("/config.json", HTTP_GET, handleConfigGET);

  /* deleted the files present on SPIFFS file system */
  server.on("/format", HTTP_GET, [](){
    if ( !enableUpdates ) {
      server.send(200, "text/plain", "locked");
      return;
    }
    
    String result=SPIFFS.format()?"OK":"NOK";
    server.send(200, "text/plain", result);
    });
    
  server.on("/reset", HTTP_GET, [](){
    server.send(200, "text/plain", "restarting...");
    ESP.restart();
    });

  server.on("/unlock", HTTP_GET, [](){
    if( server.arg("password") == "securitybyobscurity" ) {
      enableUpdates = true;
      httpUpdater.setup(&server, "/update");
    }
    
    server.send(200, "text/plain", "enableUpdates: "+String(enableUpdates));
    });

  server.on("/log", HTTP_GET, [](){
    std::deque<String>::const_iterator i;
    String response = "Log:\n";
    uint32_t counter = 0;

    for(i=log_messages.begin(); i!=log_messages.end(); ++i){
        response += String(counter) + ": " + (*i) + "\n";
        counter++;
    }
    
    server.send(200, "text/plain", response);
    });
  
  server.on("/edit", HTTP_POST, [](){ 
    if ( !enableUpdates ) {
      server.send(200, "text/plain", "locked");
      return;
    }
    
    server.send(200, "text/plain", "");
    }, handleFileUpload);

  server.begin();
}

/******************************************************************************
Description.: handle HTTP-clients
Input Value.: -
Return Value: -
******************************************************************************/
void loop_webserver() {
  server.handleClient();  
}
