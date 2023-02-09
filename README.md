# audio_stream_test
Microphone streaming through network test application

###### Dependencies:
 * libmp3lame with development headers
 * pulseaudio with development headers

###### How to build:
```
git clone && cd cloned_dir
mkdir build
cd build
cmake ../
make
```

###### How to stream audio:
```
./source/audio_src -s 127.0.0.1 -p 1234 -r 44100 -b 320
```

###### How to receive audio stream:
```
./source/audio_play -p 1234 -r 44100 -B 2
```

###### Options:
 - -s - ip address to streaming
 - -p - UDP port
 - -r - audio rate
 - -b - mp3 bitrate
 - -B - pulseaudio prebuf size, default is 2; increase it if sound crackling
