# android

Android 8+; targets Android 16. No ads or purchases.
Chats, images, voice input, requests, notifications, stats, export.

Enter your Hermes URL and login. Private HTTP needs the checkbox.
First install starts empty; the headless API works without a web interface.

From this folder: `gradlew assembleDebug` with an Android SDK and JDK 17+.
Windows setup: `python scripts/bootstrap-toolchain.py`, then `scripts/build.ps1`.
Keep `.signing` private and backed up; it owns app updates.
