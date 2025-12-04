# Gesture-Controlled Music Player

## Project Overview
I built a contactless music player that responds to hand gestures in mid-air. By waving your fingers left, right, up, down, or tapping above the sensors, you can control music playback without touching the speaker. The system uses three distance sensors arranged in a triangle to track hand movements (distance and velocity metrics wrt each sensor) and translates them into music controls. Swiping left or right changes tracks, swiping up or down adjusts volume, and a quick tap pauses or resumes playback. A physical button also serves as a master enable/disable switch for the gesture system along with an led that represents the different states of the embedded system.

## Hardware Components

**VL53L0X Time-of-Flight Sensors (3 sensors):** The sensors are arranged in a triangular arrangement (left, right, and top positions) such that they enable proper X-Y spatial mapping by determining which sensor detects the hand first. When you swipe left, the left sensor triggers before the right. When you swipe up, the bottom two sensors (left and right) detect motion before the top sensor. This geometric layout makes directional classification possible. A key technical challenge was the low sample rate of the sensors and ensuring each sensor operates independently on the I2C bus without interference. Each one had to be assigned a unique address (0x30, 0x31, 0x32) using the XSHUT pins to power cycle them individually during initialization. The system samples all three sensors at roughly 50 Hz, and the preprocessing pipeline applies median filtering, exponential moving average smoothing, and nearest layer gating (that enables tracking the closest object within a 20mm depth window and filtering out background noise).

**MAX98357A I2S Audio Amplifier:** This component handles the actual audio output by converting digital signals into analog sound using its on board DAC. It connects directly to the ESP32 via the I2S protocol, allowing me to stream audio without using the processor intensive DAC conversion. This frees up the microcontroller to focus on gesture processing and sd card logging.

**SD Card Module:** Stores all the WAV music files. The system reads audio data from the SD card and streams it to the amplifier. The SD card also stores system logs that track gestures and any errors for debugging. The SDCardManager code, is a code that enables uploading music and downloading log files from the sd card via a website that can be visited by connecting to the ESP32 wifi.

**ESP32-C6 Microcontroller:** The controller runs FreeRTOS to handle multiple tasks simultaneously. One task continuously reads sensors and detects gestures, while the other manages audio playback.

**RGB LED:** Provides visual feedback about the system state. Green means idle and ready, blue indicates a gesture was recognized, yellow/orange shows an unclear gesture, and red indicates an error or that the system is disabled.

**Push Button:** Enables on/off capabilities for the system.

## Challenges & Solutions

The biggest challenge was filtering out false positives from the sensors. Initially, the system would trigger gestures from background objects, slight hand trembles, or sometimes even shadows or unexplained noise. I solved this by implementing a multi layered filtering approach. First, a median filter smooths out individual bad readings based on the last 3 values. Second, an exponential moving average reduces noise fluctuations. Third, some gating for values that ensures to only track the closest object (your finger and not the whole hand) and ignore anything behind it. This combination dramatically improved accuracy.

Another issue was the low sensor sampling rate and distinguishing between different gesture types with similar sensor patterns. A tap and a horizontal swipe both activate the left and right sensors, but taps happen very quickly with high velocity while swipes show a clear timing difference between sensors. Additionally,noise from adjacent sensors would sometimes create false readings. While the sensors I used offer much lighter processing overhead compared to camera based systems, noise management and classification based on raw distance data became a critical challenge. I addressed this by adding velocity tracking and first seen timestamps to each sensor, which allowed the classifier to reliably distinguish between gesture types by looking at the temporal sequence and speed of activation rather than just which sensors were triggered. Even with these solutions, the low sample rate hurt performance, but slow gestures worked well.

An important design tradeoff emerged around classification approach. I considered implementing a tiny ML model for gesture recognition based on the processed data, which would be more robust to environmental variations and learn relative patterns between sensors. However, my preprocessing pipeline and continuous audio streaming were already computationally intensive, and when I attempted to deploy WiFi capabilities alongside the existing code, the system struggled to maintain the 50 Hz continuous sampling rate.In addition the professor recommended me to try to develop a non machine learning approach in the project proposal feedback, which later made sense given the computational constraints. Considering these constraints, I made the deliberate choice to use a simple rule based threshold system instead, prioritizing computational simplicity and real time performance over classification confidence. This allowed the system to handle simultaneous gesture detection and audio playback. The drawback became apparent during demo day when the ToF sensors produced different absolute distance readings depending on ambient lighting and surface reflectivity. I had tuned all thresholds in my development environment, but when I analyzed the logged data from the SD card after demo day, the magnitude of sensor readings was significantly lower than expected. This environment dependency caused occasional misclassifications and reduced accuracy in the new setting, a direct consequence of choosing simple thresholds over adaptive ML based classification.


## What Makes This Different

Due to the dynamic nature of human gestures, most gesture classification systems rely on cameras and computer vision systems that require significant processing power and work poorly in low light. My goal was to try out an approach that uses simple, inexpensive time of flight sensors that work perfectly in any lighting condition and require minimal computation. The entire gesture recognition algorithm runs locally on the microcontroller without any external processing or cloud connectivity.



## Future Improvements

**Automatic Calibration System:** Implement a startup calibration routine that reads baseline distance values from idle sensors in the current environment, then dynamically adjusts classification thresholds based on these readings. This would solve the environment dependency issue by adapting to different environments automatically. The system could periodically recalibrate during idle periods to maintain accuracy over time.

**TinyML Gesture Classification:** Replace the rule based threshold system with a lightweight neural network trained on gesture data from multiple environments. This would provide robustness against environmental variations while keeping inference overhead manageable. This addition comes with a tradeoff, that is, hardware capabilities. With a more capable microcontroller, or one with hardware acceleration features, a small model could run efficiently alongside the audio system, learning relative patterns between sensors rather than relying on absolute distance values.

**WiFi Music Upload Interface:** Integrate the existing WiFi file manager capabilities directly into the main system so users can wirelessly upload new songs to the SD card through a web interface. Currently the WiFi code exists separately, but combining it with gesture control would require optimizing the continuous sampling loop to handle network traffic effeciently. This would eliminate the need to upload a new sketch or remove the SD card every time you want to update your music library and would make this embedded system almost stand alone.


