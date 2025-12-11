# PAwR experiments

This repository containse a one to many system based on PAwR protocol
introduced in BLE 5.4. It is meant to be run on nrf54l15-dk.

To show the advantage of PAwR over previously available advertiser based
one to many system response slots where used for ACK. Additionaly authorisation
is performed using HMAC-SHA256 and counters.


The project is composed out of 3 parts:
- advertiser - BLE advertiser
- scanner - BLE scanner
- crypto_flasher - a binary intended for flashing cryptografic keys on to the devices.

You will first need to generate keys and then flash each board with crypto_flasher.
Only afterwards it will be possible to flash either advertiser or scanner binaries.

> Side note: if you happen to need a mini rack for a bunch of nRF54l15-dks, [here it is](https://github.com/avalanche-pwn/nrf54l15dk-rack).

## Recognition
This was made as a part af my bachelor thesis under supervision of PhD Marek
Bawiec and DSc, PhD, Eng, Maciej Nikodem from Wrocław University of Science and
Technology.

## Fetching the code
For simplicity for both building and flashing it is recommended to use dev env provided
[here](https://github.com/avalanche-pwn/nrf54l15-dk-devenv). Please first follow installation
step there.

It should also be possible to build in flash it without the mentioned dev env,
to do that one would need to install [nRF Connect SDK version 3.1.1](https://docs.nordicsemi.com/bundle/ncs-3.1.1/page/nrf/installation.html).
After installation it should be possible to follow the commands below however this was not tested.
Also note that the commands below assume that the $BOARD env variable is set to a proper target
which is nrf54l15dk/nrf54l15/cpuapp/ns (note the ns part which is important for TFM compatibility).
The devenv mentioned above sets it up accordingly.

If you are using the dev environment enter it using:
```
./build_env.sh --docker nrf --enable-ns
```
(This specifies to use Nordic based docker environment and to use the non secure target with TFM enabled).

Afterwards initialise the project with:
```
west init -m https://github.com/avalanche-pwn/pawr-experiments-combined.git pawr-experiments-combined
```
This will create pawr-experiments-combined directory. Afterwards it is necessary to install ncs and all 
the dependencis to do that cd into pawr-experiments-combined directory and run:
```
west update
```

## Key management
The project contains a west command extension allowing for key generation and
management for details run:
```
west keymgr --help
```

It is important that before flashing a particular scanner it's key is already
generated. Before flashing an advertiser all the keys need to be generated (Since
advertiser needs to hold the keys of all scanners).

All of the devices in this project are assigned an id which is a 2 byte
unsigned integer. The value 0 is reserved for advertiser, other devices
have id's ranging from 1 upwards. The `west keymgr` command also handles
assignment of the Nordic device ids to the system ones.

To generate a key for a device enter the pawr-expriments-combined directory created during *Fetching the code* fase.
Run:
```
west keymgr --generate --dev-i INTERNAL_DEV_ID --manufacturer-id MANUFACTURER_DEV_ID
# For example the below will generate a key for advertiser with manufacturer id 1057707291.
west keymgr --generate --dev-i 0 --manufacturer-id 1057707291
```
After generating the first key a file keys.json should get created and look something like this:
```
{
  "0": {
    "manufacturer_id": "1057707291",
    "key": "d029beaa91a0e945a8c376b23d5c56206da10ddf20cf599468d399a3ff71ba5d"
  }
}
```
> Note obviously if you want this to remain secure don't post your keys.json online.

After you generated all the keys you may need it's time to flash crypto_flasher binary
which will use PSA to safely save the keys onto the device.

First you need to build the crypto flasher. Enter the
pawr-experiments-combined/pawr-experiments-combined.git/crypto_flasher
directory.

Run:
```
west build -- -DCONFIG_FLASHED_DEVICE=DEVICE_ID
# for example
west build -- -DCONFIG_FLASHED_DEVICE=1
```
After building it is necessary to flash the device.
With many devices connected to the computer it is quite easy to make mistakes when chosing which device to flash.
To make it easier the keymgr provides an interface to fetch the manufacturer id from the internal device id.
To do that you can:
```
west keymgr --dev-id 1
```
Hence the easiest way to flash a device with internal id 1 would be:
```
west flash -i $(west keymgr --dev-i 1)
```

## Building and flashing
### Building and flashing advertiser

> Remember it's necessary to first at least once flash the device with crypto_flasher
> in order to flash its cryptographic key onto the board. Additionaly before you
> flash the advertiser all the keys you ar going to use need to be generated.

To build the advertiser code you need to enter the advertiser directory inside
of pawr-experiments-combined.git.
Afterwards do
```
west build
```
and then (also from the same directory) you can flash the advertiser using:
```
west flash -i $(west keymgr --dev-i 0)
```
> Remember advertiser always needs to have id 0.

### Building and flashing a scanner

> Remember it's necessary to first at least once flash the device with crypto_flasher
> in order to flash its cryptographic key onto the board.

To build the scanner code you need to enter the scanner directory inside
of pawr-experiments-combined.git.

Each scanner needs to have it's internal id configured, so first run:
```
west build -- -DCONFIG_SCANNER_ID=DEV_ID
# for example
west build -- -DCONFIG_SCANNER_ID=1
```
To flash the device use:
```
west flash -i $(west keymgr --dev-i 1)
```

# Other notes

If you wish to reset advertiser during the operation please make sure
to use Button 0 instead of reset. Once the reset button is used
the advertiser will lose count of it's counter used for cryptographic
verification and afterwards the scanners won't be able to reauthenticate.

If you accidentally use the reset button you will need to reset all the
scanners as well.
