---
title: "Pick And Place Machine"
github: "https://github.com/Dari2008/PickAndPlace"
description: "I'm Building a Pick and Place machine for PCB assembly from a broken Creality 10S. Its going to have Parts Feeder Fiducial alignment as well as part alignment and a lot more cool features."
created_at: "2026-11-19"
total_time: "124h 30m"
---

# September 23, 2026: Created a Hat for the Raspberry PI (23.09.26)

I started by searching for a good controller for the Pick And Place machine. I considered a ESP32 S3 but that one would be too slow for the LCD screen and motor communication etc.
In the end I chose the Raspi because its easy to programm for and has enough computational power for the object recognition for feducials and part orientation.

After settling for the Raspi I searched for components and Footprints for the Raspi-Hat for the main controller.
![](https://fabricate.hackclub-assets.com/ee8a8b12d653aa23250363f3ef57e7cd5dfe48eced12a30465247ae092d940ca/image-png/image.png)
![](https://fabricate.hackclub-assets.com/4daf311fcf6c168034d1e9fca5fd7c8183e64c0d5b28d480401a93b339bb6645/image-png/image.png)

I also placed and routed the components / wires and ordered them... Well there was a mistake because of which I had to solder the screen wires by hand instead of it beeing a cable I could take off.
And they have arrived by now:
![](https://fabricate.hackclub-assets.com/3bbe09fdba7b8a7e2361292341ff0f361c67ff5465cbde2168b5ce867087e9f9/image-jpeg/IMG_6993.jpeg)

**Total time spent: 10h**

# October 2, 2026: Disassembling the Creality 10S (2.10.26)

I had to disassemble the Creality 10S to get the Alumium profiles and motors as well as the main Controller and screws.
![](https://fabricate.hackclub-assets.com/eb7160acacf57e095fbf5e4c69568a92ae220f72f3ce88b30b1f7148ca25cfd9/image-png/image.png)


after disassembly I found out that the Creality motherboard doesnt have enough Connectors for 8 Nema 17 Motors and thus had to search for another Motherboard. I chose the Bigtreetech Octopus since it has a lot of interfaces and Motor controllers while also supporting the TMC2209 Stepper drivers.

**Total time spent: 7h**

# October 5, 2026: Create 3d Model of Main Frame (5.10.26)

Now I have all the lengths of the Aluminium Profiles and can finally design and think about how I want to assembel it.

Its going to be 550x460mm of working area (minus a bit).
![](https://fabricate.hackclub-assets.com/94aa026e5a4076894b5416b2fe2c0703cecc0badef2245ccbc7667f91e43361b/image-png/image.png)

Now I have to drill a lot of holes for the screws to pass through the Profile and screw into the next one. I also had to tap a few holes which was my first time doing that.
Luckily I could reuse quite a lot of the motor mounts from the Creality 10S.
![](https://fabricate.hackclub-assets.com/732bfebe695b725e3c671f72c188377364c465f84388d0d160137fce67cba0f5/image-jpeg/IMG_6878.jpeg)
![](https://fabricate.hackclub-assets.com/fa66aebc2704287bfa14b07de0cf782db2a0a4282e539f3622522f147a218826/image-jpeg/IMG_6880.jpeg)

**Total time spent: 20h**

# October 12, 2026: The Head / Paste extruder (12.10.26)

After finishing the main frame I thought of the Moving head which features I wanted:
- Part Pickup / Rotation
- Paste extruder

I then thought of mechanisms that could switch between two heads and watched a few Youtube videos for inspiration. I settled on a seesaw which could also move the part picker down with force for a Nozzle changer since some parts I want to place are smaller than others and they need a smaller nozzle.
I'm planning on 5 Nozzles which can be switched.
![](https://fabricate.hackclub-assets.com/6cb18c03046847540cc2194a753a04cc484b66e879f48da6e261eec97089cdef/image-png/image.png)


Afterwards I searched for a Aluminium plate that would be big enough for the Z travel and the camera and motor I couldnd find a fitting one from the Creality so I had to buy a 3mm thick aluminium plate which I then had to cut out using a vibratory saw. It was a lot of work...
Then I printed the design of where the holes had to be and drilled them. Saidly some of the holes were not at the correct positions (milimeters off) and thus the screws of the linear rails didnt fit. There were enough though. In the mean time where I drilled holes and cut it out I also 3d printed the camera mount as well as a cable chain mount and a few cable clamps:
![](https://fabricate.hackclub-assets.com/c5812e538311fe42ca9116221b7e7063f8e204dca5113e93c3acbcbcd511144b/image-png/image.png)

Beforehand I also designed a mechanism to extrude paste from a syringe:
![](https://fabricate.hackclub-assets.com/9f15ff5bd8e3dd809562df1e4e0a7b4cbcde67f3a8a002b0f60128130b6b6816/image-png/image.png)

I then assembled it all:
![](https://fabricate.hackclub-assets.com/062ce8d82a3675ca99d572f3a295d26b5a4c002d00a656dd5c9afab3dcd02656/image-jpeg/IMG_6994.jpeg)

**Total time spent: 19h**

# October 20, 2026: Nozzle Changer (20.10.26)

I wanted something easier and designed a nozzle changer where the Nozzles of the picker can be switched (I'm using Juki PNP Nozzles).

I also wanted sensors which could check if there was a nozzle inserted or not to make sure the head either doesnt have one or has one picked up.

I also had to solder the sensors to a perf PCB which also has a WCH CH32V003 whcih checks the sensors and is connected to the Motor Controller Board (Bigtreetech Octopus) (https://github.com/bigtreetech/docs/blob/master/docs/img/Octopus/Octopus_Title.webp)

![](https://fabricate.hackclub-assets.com/10bff4eacc945ffce6ea9050c88369b8420944ebab38ab5bd1493f5277031cbd/image-png/image.png)

**Total time spent: 7h 30m**

# November 5, 2026: Housing / Mounting (5.11.26)

I thought of where I can put the Raspberry pi and Bigtreetech and all the main parts like the Vakuum pump or the power supply which is also recycled from the creality 10S, and the bottom camera which is recycled from a document camera which would have been thrown away in my school.

I first thought about mounting everything in the Creality 10S housing where it was originally mounted but then (after first trying it 

![](https://fabricate.hackclub-assets.com/eebe3bd5609374a9ecd00efcc08b3376f1929dad7d0773a7af4d34eff439734e/image-png/image.png)
)

I found out that it would be quite annoying to change stuff and upload code to the Octopus) 

I also had to cut in a slot in the Housing to even feed all the cables from motors to sensors through
![](https://fabricate.hackclub-assets.com/eb1d11f96019d790a9059f59fa48fda825d1f43b10191acec5976140505ad4a4/image-jpeg/IMG_6997.jpeg)
![](https://fabricate.hackclub-assets.com/d44926300d2dfb7356b30cd265d45a82abf5dc3f4f8cc6e69d2fdcb8f06cac7d/image-jpeg/IMG_6995.jpeg)
![](https://fabricate.hackclub-assets.com/3a5bb7307e756d036337b45492f41ee41ff43a0471a06c658c58c0faabae392c/image-jpeg/IMG_6996.jpeg)

In the end I decided to mount everything directly on the Main plate which I got from our hardware store. It was a bit too big thus I had to cut it shorter which took forever and was quite messy with all the dust.

Later I mounted all the components including the frame and everything to it
![](https://fabricate.hackclub-assets.com/6edeb763dce65baa0aaeeaa7c15fcee430d2d3859c5d4c0e057e290065a304bb/image-jpeg/IMG_6998.jpeg)

**Total time spent: 20h**

# November 10, 2026: Screen / Joystic holder (10.11.26)

Since I didn't use the housing from the Creality. I had to design my own screen and joystic holder which was quite annoying since I had removed the old joystic from the module and soldered my own one (a hall joystic module) on but that one wasn't centered so I had to do a lot of revisions to get it somewhat right.

![](https://fabricate.hackclub-assets.com/c9f6ec96cf860d44f151b1cb0a6ef7f7b298b78558d2a43c5b5f419b73ad0da1/image-png/image.png)


I also bought another 10mm thick wood plate which is going to be the main work area. its smaller than I tought but still pretty big. (For a future idea I'm planning to build a Vakuum table to hold the PCBs down instead of screwing them into the wood.)

**Total time spent: 3h**

# November 13, 2026: Vakuum Pump Mount (13.11.26)

I'm using a small 12V pump which also has an inlet which I use to create the vakuum and suck parts up. #
![](https://fabricate.hackclub-assets.com/ecc8b3972256d788d1f39c0004877d894d399b070f0a0b14dbcf01ff5c68b8f1/image-png/image.png)
For the pumps size its quite strong. I also considered that if the vakuum would release the part good enough I would add a valve to open it to athmospheric preasure so it gets reales better but I don't think I need it anymore (haven't tested yet :-).

I heard that these pumps if mounted rigidly cause quite a lot of noise because of thair vibrations. Thats why the mount has 4 long screws and the pump is suspended in springs. (Probably overkill but I wanted to do something with springs)

![](https://fabricate.hackclub-assets.com/43cbb21ff6613c72384121e8a0bd1279f1a1837487a1b0953509780e3f92a369/image-png/image.png)

**Total time spent: 5h**

# November 19, 2026: Parts Feeder (19.11.26)

Since I wanted a parts feeder which would feed the parts from a Reel so the nozzle can pick them up easely I added a beam at the back where a carriage moves on the 2020 Alu Profile beam and has a motor with a gear wich meshes with the "cartriges / reels" which get moved by a small teethed gear which meshes with the holes in the tape. I didn't want to have a motor in every "cartige" so thats why the motor moves and just meshes with the gears on top of the feeder. 
![](https://fabricate.hackclub-assets.com/56c81d4cd921975e5b7a1e4d7e73800bb36ea1d13324924dbd426d1a9c108913/image-jpeg/IMG_6999.jpeg)

These feeders are in thair 2. version and now also work with springs which hold them onto the beam.

This was definetly the hardest part until now because there is so much mechanical stuff.

![](https://fabricate.hackclub-assets.com/c764ff5f1d3a83f453c64156dd84ee0ed55fbd571e9275f1925dd546c4811b5b/image-png/image.png)

**Total time spent: 20h**

# November 19, 2026: Testing (Homing) / Endstop design

I could finally test the software out on the real hardware and it somewhat worked altough homing had quite a few bugs still for example it not stopping or it stopping too early. In the end it works now but I have to set defined endstops (aka blocks that stop there everytime since the plactes are not the same size (the side plates))

![](https://fabricate.hackclub-assets.com/8b27516cdb2fef07e6c08d567e50bf1fb6f4bc5e499b68b90b04c31a1ac7957b/image-png/image.png)

Still have to print it and test if it works.

Here a video of it working (aka Homing):
![](https://fabricate.hackclub-assets.com/ba24e27657ff39e2678136763b97e3236b889a5d9beda8cd599f6110895afd1e/video-mp4/homing.mp4)

**Total time spent: 5h**

# November 20, 2026: Vakuum Sensor fixed (19.11 / 20.11.26)

After pulling the cables through the cable chain and crimping the connectors some of the cables didn't work and measured Open Loop thus the sensors didn't work. I had to recrimp like 50% of the cables which took forever. In the end I'm happy that the cables weren't broken. (Still have to make the top camera work)

Now homing and all the sensors that didn't work work now:
- Weight Sensor (for homing the Z height)
- Vakuum Sensor (for checking weather a part is picked up or not)
- Some endstops (Z-Axsis)

I also changed the top lights since they were 24V but I supplied only 12V because I didn't have a 24V power supply and didn't want to have to add one. (Works now :-))
![](https://fabricate.hackclub-assets.com/6d2f67c54ac4d4c830d445f14db9d5ca8f41920e72029ce5bfcc26cdbad5a894/image-jpeg/IMG_7013.jpeg)

**Total time spent: 5h**

# November 21, 2026: Fixing Top Camera 

Last time the bottom camera didn't work. Now it works. I had swapped D- and D+ and some of the crimps on the cable failed a second time :-(. 
I also tried to power the raspberry pi from the 12V to 5V converted but that didn't work. It didn't have any output. That's why I had to search for another way and switched to a PCB module not direct cable to USB-C converter.
![](https://fabricate.hackclub-assets.com/786360d6dd37a9b4fbbac6dce24bbf1227edf7b258de07cb54445d68f2c29e50/image-jpeg/IMG_7020.jpeg)


Now I can finally try and test if everything works.

Also the lights now work.
![](https://fabricate.hackclub-assets.com/4c01562661b9ca5e18b3a03f45cdab28978e9880241fdb169e5f8bdbc1c9c7db/image-jpeg/IMG_7019.jpeg)

**Total time spent: 3h**
