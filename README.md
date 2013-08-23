wemed
=====

WEbkit-based Mime EDitor.

Wemed can view, edit and create documents in MIME format. This includes email messages (.eml files) and MIME HTML archives (.mht or .mhtml files). Wemed uses Webkit to display HTML.

Wemed is in beta. It is useful, but still has some rough edges.

See
- http://en.wikipedia.org/wiki/MIME
- http://en.wikipedia.org/wiki/MHTML


Building
--------

Wemed depends on webkitgtk-3.0 and gmime-2.6.

Arch Linux

	# pacman -S gmime webkitgtk

Ubuntu/Debian

	# apt-get install libgmime-2.6-dev libwebkit-3.0-dev

From here the process is standard:

	$ git clone https://github.com/ohwgiles/wemed.git
	$ mkdir -p path/to/build && cd path/to/build
	$ cmake path/to/source
	$ make



