# S9-C SDR Receiver Support for Websdr & Openwebrx

S9-C SDR Support max 8 channel 192khz band

  - Websdr cfg
```sh
band ch1
device /tmp/s9c-fifo-ch1
samplerate 192000
centerfreq 1000
swapiq
```

  - openwebrx cfg
```sh
start_rtl_command="cat /tmp/s9c-fifo-ch1"
format_conversion="csdr convert_s16_f"
```
