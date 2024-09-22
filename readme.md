# Welcome to HighPhone!

![HighPhone Coverimage](/documentation/HighPhone.webp)

This repo contains code for PlatformIO and was developed with Visual Studio. It's code for an ESP32 that's built into an old rotary dial phone. Current features:


-Playback wav-files from SD card when number is dialled
-Host Webserver with configuration-website
-Website Features:
	-Play Samples (phone rings, sample plays when picked up)
	-Cancel Call
	-delete sample
	-upload sample



Planned Features:
-Push-to-Talk button on website to quickly record a message and save it to sd card
-rename sample through website
-More sounds, when waiting for dial, call ended and other situations
-build robots that can be navigated through digits dialled
-build an adventure game using such a robot
-Read Microphone - enable users to dial a number, leave a message on a "voice box" and then listen to it when the number is dialled again
-Multiple Phones - have multiple phones that connect via wifi and build an audio stream so people can call other phones