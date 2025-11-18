# SwitchbotBot-ESP32-BLE-Gateway
BLE gateway to control Switchbot Bot device with ESP32, either over Matter or through the Web Server
<br><br>

* Connect with any Matter hub and this will appear as a On/Off switch
  
* Access through the built-in async web server (uses request continuation feature):
  
  * http://<ip_of_the_device>/switchbot/press - to execute single press command
  * http://<ip_of_the_device>/switchbot/command?cmd=570200 - to i.e. get Bot status like battery level (commands need to be in hex)
  * result of any API call is returned to the browser in the JSON format, i.e. { "status": "01", "payload": ff00 }

Valid commands and Switchbot Bot API is available here: 
https://github.com/OpenWonderLabs/SwitchBotAPI-BLE/blob/latest/devicetypes/bot.md
