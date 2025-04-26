## This project shows live feeds from ESP32-CAM

1. Create `secret.h` file on root and add following in it.
   ```
   #define USER_NAME "WIFI_NAME"
   #define PASSWORD "WIFI_PASSWORD"
   ```

2. Flash the data to ESP32-CAM as `CAMERA_MODEL_AI_THINKER` board type.

3. Connect to [http://ADD_IP_ADDRESS_SHOWN_IN_TERMINAL/stream](http://IP_ADDRESS_SHOWN_IN_TERMINAL/stream)