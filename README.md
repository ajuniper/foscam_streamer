# foscam_streamer
Simple C program for streaming audio and video from a Foscam FI8918W PTZ camera

Compile using `gcc -o fostream fostream.c`

Run using:
Receive video: `./fostream video CAMERA_IP CAMERA_PORT CAMERA_USERNAME CAMERA_PASSWORD >outfile.mjpeg`
Receive audio: `./fostream audio CAMERA_IP CAMERA_PORT CAMERA_USERNAME CAMERA_PASSWORD >outfile.ima`
Send audio: `./fostream talk CAMERA_IP CAMERA_PORT CAMERA_USERNAME CAMERA_PASSWORD <infile.ima`

Audio is 8KHz ADPCM - import as raw `VOX ADPCM` into Audacity to manipulate recorded content, export as same to create output.

Sent audio is a bit corrupted, not quite sure why...

References:
* https://www.instructables.com/Hack-a-30-WiFi-Pan-Tilt-Camera-Video-Audio-and-Mot/
* https://www.openipcam.com/files/Manuals/Bseries_VideoAudioAccessProtocol.pdf

Todo:
* better argument parsing
* support multiple concurrent streams
