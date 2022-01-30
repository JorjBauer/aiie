#!/usr/bin/env perl

use strict;
use warnings;

#expects RGB 24-bit data

my $infile = shift || die "give me a file name, with raw bytes in it";
my $size = (-s $infile);

open my $fh, '<', $infile || die $!;

my $count = 0;
my ($bufr, $bufg, $bufb);
while (my $r = read $fh, $bufr, 1) {
    my $g = read $fh, $bufg, 1;
    my $b = read $fh, $bufb, 1;

    my $v16 =
        ((ord($bufr)&0xF8) << 8) |
        ((ord($bufg)&0xFC) << 3) |
        ( ord($bufb)       >> 3);

    printf("0x%.2X,0x%.2X, ", ($v16 >> 8), $v16 & 0xFF);

    if (++$count == 8) {
        print "\n";
        $count = 0;
    }
}
printf("\n");
