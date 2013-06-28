#!/usr/bin/perl -w
# $Id: camsource2bmp.pl,v 1.2 2003/01/18 16:57:00 dfx Exp $

# Example script to demonstrate how to access camsource's raw image data.
# It connects to the given url and writes the data to a .bmp file.

use strict;
use Socket;

##########################################

sub main() {
    if (!@ARGV) {
	usage();
	exit(0);
    }
    
    my $url = shift(@ARGV);
    my %url = parseurl($url);
    die("Invalid url given") if (!defined($url{host}));
    die("Don't know how to handle '$url{protocol}' protocol") if (lc($url{protocol}) ne "http");
    
    my $fd = connectto($url{host}, $url{port})
    	or die("Unable to connect to $url{host}:$url{port}: $!");
    sendrequest($fd, $url{path});
    
    my @headers = getheaders($fd);
    my $httpresp = shift(@headers);
    
    die("document contains no data")
    	if (!defined($httpresp));
    die("Got http response '$httpresp'")
    	if ($httpresp !~ m,^HTTP/[\d.]+ 200\b,);

    my $conlen = grepheader("content-length", @headers)
    	or die("missing content-length header");
    my $x = grepheader("x-image-width", @headers)
    	or die("missing x-image-width header");
    my $y = grepheader("x-image-height", @headers)
    	or die("missing x-image-height header");
    die("content-length doesnt match height x width")
    	if ($x * $y * 3 != $conlen);

    my $data;
    read($fd, $data, $conlen);
    close($fd);
    
    die("error reading data: $!") if (length($data) != $conlen);

    my $outfile = shift(@ARGV);
    if (!defined($outfile)) {
	$outfile = $url{path};
	$outfile =~ s,^.*/,,;
	$outfile .= ".bmp" if ($outfile !~ /\.bmp$/i)
    }
    open(OUT, "> $outfile")
    	or die("Error opening $outfile for writing: $!");
    
    my $header = bmpheader($x, $y);

    print OUT ($header);

    # bmp data is bottom to top in bgr order
    my $line = $y - 1;
    my $row = 0;
    while ($line >= 0) {
	my $row = 0;
	while ($row < $x) {
	    my $offset = ($row + $line * $x) * 3;
	    my $bgr = substr($data, $offset + 2, 1) .
		substr($data, $offset + 1, 1) .
		substr($data, $offset, 1);
	    print OUT ($bgr);
	    $row++;
	}
	$line--;
    }
    
    close(OUT);
    print("$outfile written\n");
}

##########################################

sub usage() {
    print(<<"!");
Usage:
	$0 <url> [file.bmp]
Example:
	$0 http://localhost:9192/raw

This script connects to the given url, retrieves the raw image data and
writes it to a standard bmp file. The url should point to a resource
provided by a camsource process, and it should be configured to deliver
raw image data. If the filename is not specified, it defaults to the
file part of the url with ".bmp" appended.
!
}

##########################################

sub parseurl($) {
    my ($url) = @_;
    
    my @parts = ($url =~ m,^((\w+)://)?([\w.]+)(:(\d+))?(/[^?\#]*)?(\?([^?\#]*))?(\#([^\#]*))?$,);
    
    my @partindexes = (
    	[qw(protocol 1 http)],
    	[qw(host 2)],
    	[qw(port 4 80)],
    	[qw(path 5 /)],
    	[qw(query 7)],
    	[qw(anchor 9)],
    );
    my %ret;
    foreach my $index (@partindexes) {
	my $part = $parts[$index->[1]];
	$part = $index->[2] if (!defined($part));
	$ret{$index->[0]} = $part;
    }
    
    return %ret;
}

##########################################

sub connectto($$) {
    my ($host, $port) = @_;
    
    my $fd;
    socket($fd, AF_INET, SOCK_STREAM, 0)
    	or return;
    my $ip = inet_aton($host)
    	or return;
    my $sin = sockaddr_in($port, $ip);
    connect($fd, $sin)
    	or return;
    my $oldfd = select($fd);
    $| = 1;
    select($oldfd);
    return $fd;
}

##########################################

sub sendrequest($$) {
    my ($fd, $path) = @_;
    
    print $fd ("GET $path HTTP/1.0\n\n");
}

##########################################

sub getheaders($) {
    my ($fd) = @_;
    
    my @ret;
    for (;;) {
	my $line = <$fd>;
	last if (!defined($line));
	$line =~ s/[\x0d\x0a]*$//;
	last if ($line eq "");
	push(@ret, $line);
    }
    
    return @ret;
}

##########################################

sub grepheader($@) {
    my ($needle, @haystack) = @_;
    
    foreach my $header (@haystack) {
	if ($header =~ /^\Q$needle\E:\s*(.*?)$/i) {
	    return $1;
	}
    }
    
    return;
}

##########################################

sub bmpheader($$) {
    my ($x, $y) = @_;
    
    my $fileheaderformat = "a2VVV";
    my $imageheaderformat = "V VV vvVV VV VV";
    my $fileheaderlen = length(pack($fileheaderformat));
    my $imageheaderlen = length(pack($imageheaderformat));
    my $headerlen = $fileheaderlen + $imageheaderlen;
    my $conlen = $x * $y * 3;
    return pack($fileheaderformat, "BM", $headerlen + $conlen, 0, $headerlen) .
	pack($imageheaderformat, $imageheaderlen, $x, $y, 1, 24, 0, $conlen, 10000, 10000, 2 ** 24, 0);
}

##########################################

main();
