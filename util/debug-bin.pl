#!/usr/bin/perl

# Reads a compiled binary (ProDOS header) and loads it via the debugger.
#
# Binary should be created and loaded something like
# 
# cc65 -t apple2enh -O testbin.c 
# ca65 -t apple2enh testbin.s
# ld65 -t apple2enh -o testbin testbin.o --lib apple2enh.lib
# ./debug-bin.pl testbin

use strict;
use warnings;
use IO::Socket;

$| = 1;

my $filename = shift || die "No file provided";

my $fh;
open($fh, $filename) || die "Unable to open file: $!";

my $data;
die unless (read($fh, $data, 26) == 26);
my $pos = 26;

my ($magic, $version, $volume, $numEntries) = unpack("H8H8H32H4", $data);
die "Bad magic [$magic]" unless ($magic eq "00051600");
$numEntries = int($numEntries);

print "Entries: " . $numEntries . "\n";

my ($dataoffset, $datalength, $infoOffset, $infoLength, $entryPoint);
while ($numEntries) {
    die unless (read($fh, $data, 12) == 12);
    $pos += 12;
    my ($entryID, $entryOffset, $entryLength) = unpack("H8H8H8", $data);
    if (hex($entryID) == 11) {
	print "ProDOS chunk found at offset 0x$entryOffset length 0x$entryLength\n";
	$infoOffset = hex($entryOffset);
	$infoLength = hex($entryLength);
    }
    if (hex($entryID) == 1) {
	print "Data found at offset 0x$entryOffset length 0x$entryLength\n";
	$dataoffset = hex($entryOffset);
	$datalength = hex($entryLength);
    }
    $numEntries--;
}
die "Failed to find info chunk"
    unless ($infoOffset || $infoLength);
die "Failed to find data chunk"
    unless ($dataoffset || $datalength);

die "Haven't figured out how to read in this order"
    unless ($infoOffset < $dataoffset);

while ($pos < $infoOffset) {
    die unless (read($fh, $data, 1) == 1);
    $pos++;
}
die "Failed to read info chunk" unless (read($fh, $data, $infoLength) == $infoLength);
$pos += $infoLength;

my $unpacked = unpack('H*', $data);
my @hex    = ($unpacked =~ /(..)/g);
my @bytes    = map { hex($_) } @hex;
$entryPoint=($bytes[6] << 8) |$bytes[7];

while ($pos < $dataoffset) {
    die unless (read($fh, $data, 1) == 1);
    $pos++;
}

die "Failed to read data chunk" unless (read($fh, $data, $datalength) == $datalength);
$unpacked = unpack('H*', $data);
@hex    = ($unpacked =~ /(..)/g);
@bytes    = map { hex($_) } @hex;

my $socket = new IO::Socket::INET (PeerHost => '127.0.0.1',
				   PeerPort => '12345',
				   Proto => 'tcp',
				   ) or die "ERROR in Socket Creation : $!\n";
print $socket sprintf("L 0x%X\n", $entryPoint);
my $count = 0;
foreach my $i (@bytes) {
    print $socket sprintf("%.2X", $i);
    print sprintf("%.2X ", $i);
    $count++;
    if ($count >= 16) {
	print $socket "\n";
	print "\n";
	$count = 0;
    }
}
print $socket "\n"; # End data entry mode

# Goto new code
print $socket sprintf("G 0x%X\n\n", $entryPoint);
<$socket>;
#sleep(5);
print sprintf("EntryPoint: 0x%X\n", $entryPoint);
exit(0);

__END__
Each entry:
  Entry ID (4 bytes): 00 00 00 01 = data form
  Entry Offset (4 bytes): 00 00 00 3a = 0x3a
  Entry Length: 4 bytes: 00 00 18 40 = 0x1840 bytes
---
  00 00 00 0B = ProDOS file info
  00 00 00 32 = offset
  00 00 00 08 = length
---

Header w/ size of $3A bytes; then file data

Offset $24/$25 are length (big endian)
$35 -> file type ($06: BIN; $FF: SYS)
$38/$39: aux type for bin (big endian)

$33: ProDOS Access Byte %11000011 (enable access, destroy, rename, write, read)
