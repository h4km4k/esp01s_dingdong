# esp01s_dingdong
Software and schematics for Siedle 6+n (should work for every bell that works with 12V) Doorbell, Dooropener and morse decoder.

The code was written almost exclusively by sschori <https://github.com/sschori/ESP01RelayModul>. Thank you for that.

I just removed a memory leak and added the doorbell/opener/morse code.

I made some "security by obscurity" with hiding the settings link ;). To do the Setup flash the esp, powercycle it then connect to the "Relay-Modul" AP. Then go to <http://192.168.1.1/settings> to connect to your home wifi and set your (mdns) hostname.

In this example the door will open for 7 seconds wenn you ring this morse code: ". . - . . -"



The circuit:

<img src="https://raw.githubusercontent.com/h4km4k/esp01s_dingdong/refs/heads/main/images/circuit1.png" alt="Not_presssed" style="width:75%; height:auto;">


Here are a screenshot when the doorbell was not pressed:

<img src="https://raw.githubusercontent.com/h4km4k/esp01s_dingdong/refs/heads/main/images/Screenshot_1.png" alt="Not_presssed" style="width:25%; height:auto;">

This time with the doorbell pressed the last 30s:

<img src="https://raw.githubusercontent.com/h4km4k/esp01s_dingdong/refs/heads/main/images/Screenshot_2.png" alt="Pressed" style="width:25%; height:auto;">

