@echo off
set "ANDROID_HOME=C:\Program Files (x86)\Android\android-sdk"
set "JAVA_HOME=C:\Program Files\Android\openjdk\jdk-21.0.8"
set "PATH=%JAVA_HOME%\bin;%ANDROID_HOME%\platform-tools;%PATH%"
call "H:\esp32-led-test\app2\android\gradlew.bat" -p "H:\esp32-led-test\app2\android" assembleDebug
