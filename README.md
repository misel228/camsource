camsource
=========

camsource is a linux webcam demon originally written by Richard Fuchs. (See http://sf.net/projects/camsource where it's still hosted.) It appears to be abandoned which is why forked it over here.

The reason behind it is that I've yet to find a tool which does everything camsource does. So it's still my favorite despite two major drawbacks:

1. The code is old and doesn't compile on modern browsers
2. Missing support for Video4Linux version 2

The first one is not that complicate to fix but there are also many warnings during compilation that I'd like to fix. For the second problem I already got the code that someone else wrote (there was a camsource v0.7.1 floating around the web. It's not online anymore but I got it secured on my HDD - however this version doesn't ./configure at all).

For now it's just the original code.

# Dependencies:
* libxml
* libjec
* libv4l1
