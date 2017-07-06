# bitprim
Bitcoin development platform

## Getting started (Linux) - Build from source

1) Clone this project. In a terminal:

```sh
$ git clone --recursive https://github.com/bitprim/bitprim.git
```
If you get an error such as ```git clone https://github.com/bitprim/bitprim.git```, please install Git:

```sh
$ sudo apt-get install git
```

2) Run the script to install the platform's dependencies in your system (this will require administrator rights):

```sh
$ cd bitprim/check-dependencies
$ sudo ./Install-deps-and-build.sh
```
The dependencies are:

* Cmake 3.7.0
* Boost 1.64 (built with -fPIC flag; the script takes care of that, but beware because a default Boost install does not use this option)

Cmake and Boost build from source might take several minutes, so please be patient.
As soon as dependencies are satisfied, build will start. A successful build will look like this:

![Successful build](http://pichoster.net/images/2017/07/07/4062260796e0ce4949db7b5740e0761b.png)

## Reference documentation ##

For more detailed documentation, please refer to https://www.bitprim.org/
