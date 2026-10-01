# shima package manager

a simple package manager, built in C99 with POSIX-2001 compatibility

## compilation/installation

shima only depends on [zlib](https://zlib.net/) (via [heliotrope](https://github.com/yuiyamu/heliotrope)), so it can pretty much be compiled in any environment that you can think of (that supports C99 and POSIX-2001 at a minimum). if you'd like to compile it yourself, simply download a release tarball, run `./configure` (or `./configure --enable-debug` for ASan support which is recommended during development), and run `make`.

if you'd like to install shima system wide, all you need to do is place the binary (which can also be downloaded for various platforms) in `/usr/bin`, and create the following files/directories:

- `/etc/shima`
- `/etc/shima/sources.list`, putting `http://pkg.yuru.ca/shima/x86_64/` inside (switch to whatever non-x86 platform you may be using)
- `/etc/shima/installed.list`

which you can do with:

> `sudo mkdir -p /etc/shima && sudo echo "http://pkg.yuru.ca/shima/x86_64/" >> /etc/shima/sources.list && sudo touch /etc/shima/installed.list` 

after which, you can run `sudo shima update` to populate the package list.

## usage

it is **not** recommended that you use shima on your own system unless you know what you're doing, as you likely already have a (better) package manager installed. take it from me, i accidentally overwrote glibc on my system while developing and if you know what that means you should be rightfully scared :p the best use case for shima is for POSIX compliant systems without a package manager already, such as macos. 

with that warning out of the way, using shima is quite simple as an end user. `shima update` to update the package database with the latest information, `shima install package` to install a package (or `shima install ./package` for a local package), and `shima remove package` to remove.

### package building

where it gets a little more complicated is if you'd like to make your own packages for shima. shima uses `.campsite` files to define many things about each package, like its name, version, compilation steps, and post-install commands to run. check out the prebuilt `.campsite` files on the `pkg.yuru.ca` server, or follow this example on how to create a basic `.campsite` file:

> campsite v0.1
>
> = info =
> name: package
> desc: a nice little package
> pkgver: 1.0-shm0
>
> = sources =
> ./package-1.0.zip
>
> = depends =
> dependency-6.7-shm0
> \# we can also add comments in here!
> 
> = prepare =
> !~!shima
> ./configure --prefix=/usr
> make -j$(nproc)
> make install DESTDIR=\$(pwd)/shima
>
> = post-install =
> echo "hello :D"

## licensing

shima is 100% written by me (yuiyamu), and is strictly and forever licensed under AGPLv3 ! ~ (˵•̀ᴗ - ˵ )
