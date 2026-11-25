# CODOGOTCHI
Tamagotchi style desk buddy for competitive programming and Github :)

hi c: to run this in wokwi, youll need an esp32 board (just the standard one) then, from the little '+' icon near the circuit board, get an ssd1306 oled display, and a pushbutton (which, for now, acts as our touch sensor), and a passive piezo buzzer. open the libraries tab (should be besides files), and add adafruit ssd1306, adafruit gfx, and arduinojson. paste the diagram.json and sketch.ino and make a new file for face.h, and types.h get ur build up and running, and use the following commands in the terminal: 

HEALTH: h0 - h100
MOOD: m0 - m3
LEVEL: l1 - l50
CELEBRATION: c
SLEEP TOGGLE: z
TOUCH SENSOR DEBUGGING: t

still havent completely figured out health and mood relation, but health should be the streak and mood should be the daily thing!!

DHRUV here :)

replaced sketchino with my code, added types.h (header for sketch cuz arduino is retarded), replaced diagramjson, Let me know if the current face is nice or if you want me to add your modifications back! for now, the base functions like health, mood, celebrations, petting animation, hibernation mode work perfectly with the push button. once I get the website code and hardware I'll try to add the functionality for that as well!

priy here!!! :D

update 1: the website currently has a login screen, asking for your + codo's name, connecting cf + gh, starts at level 0 + 0 codocoins, uses scoring metric (check src/lib/scoring for metric), and the items are fully unlockable! i do think we need a log out button tho lmao
ill probably first make the changes in the other repo and then check here :D
