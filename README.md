# split8
Android 8ch speaker splitter.
Build on GitHub Actions, download artifact `split8`.

Run:
```
adb push split8 /data/local/tmp/
adb shell chmod 755 /data/local/tmp/split8
adb shell /data/local/tmp/split8 /data/local/tmp/test2ch.wav 0 24
```
