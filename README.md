wemed
=====

WEbkit-based Mime EDitor.

Wemed can view, edit and create documents in MIME format. This includes email messages (.eml files) and MIME HTML archives (.mht or .mhtml files). Wemed uses Webkit to display HTML.

See
- http://en.wikipedia.org/wiki/MIME
- http://en.wikipedia.org/wiki/MHTML


Building
--------

Wemed depends on webkit2gtk-3.0 and gmime-2.6.

Arch Linux - the required packages are in the repositories

	# pacman -S gmime webkitgtk

Ubuntu/Debian - gmime is in the repositories but ATOW webkit2gtk hasn't yet landed. You can get it from the Gnome 3 repository at www.ubuntuupdates.org:

	# apt-get install libgmime-2.6-dev
	# add-apt-repository ppa:gnome3-team/gnome3
	# apt-get update
	# apt-get install libwebkit2gtk-3.0-dev

From here the process is standard:

	$ git clone https://github.com/ohwgiles/wemed.git
	$ mkdir -p path/to/build && cd path/to/build
	$ cmake path/to/source
	$ make



