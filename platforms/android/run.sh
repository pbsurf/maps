adb uninstall com.mapzen.tangram.android
adb install demo/build/outputs/apk/debug/demo-debug.apk
adb shell am start -a android.intent.action.MAIN -n com.mapzen.tangram.android/.MainActivity
