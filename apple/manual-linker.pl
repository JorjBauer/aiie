#!/usr/bin/perl

# The ca65 assembler honors .ORG statements - but the ld65 assembler
# then destroys them. I haven't figured out how to make its config
# do what I want yet, so this is brute force...
#
# This script takes the listing output from ca65 and generates the 
# final binary object, like so:
#
# ca65 --target apple2enh mouserom.asm --listing mouserom.lst --list-bytes 50
# ./manual-linker.pl < mouserom.lst > out.bin

use strict;
use warnings;

my @memory;

while (<STDIN>) {
    chomp;
    if (/^(......)\s\s[12]\s\s(.+)/) {
	my $addr = hex($1);
	my $rest = $2;
	my @data;
	while ($rest =~ /^([0-9A-F][0-9A-F])\s+(.+)/) {
	    $memory[$addr] = hex($1);
	    $addr++;
	    $rest = $2;
	}
    }
}

# Specifcally, memory $C400..$C4FF gets dumped
foreach my $i (0xC400..0xC4FF) {
    $memory[$i] ||= 0;
    print chr($memory[$i]);
}
