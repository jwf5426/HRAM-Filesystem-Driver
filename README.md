# HRAM-Filesystem-Driver

This is my code for a hieratical RAM memory device driver to maintain the metadata of many files and the files' integrity.  I developed my own functions for opening, closing, reading, writing, and seeking files.  Additionally, I used sockets to send file data over a network to a local server that hosted my developed functions.  I received a letter of recommendation from the professor of the class, Patrick McDaniel, for my work on this project.

Files I personally wrote include cart_driver.c (open, close, write, read, seek), cart_cache.c (driver cache), and cart_client.c (sockets). 
