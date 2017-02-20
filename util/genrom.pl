#!/usr/bin/perl

use strict;
use warnings;

my $romfile = shift || die "Must provide the path to an Apple //e ROM image";
my $diskrom = shift || die "Must also provide the path to an Apple //e Disk II ROM image";
my $parallelrom = shift || die "Must also provide the path to an Apple // parallel card ROM image";

validate($romfile, 32768, "an Apple //e ROM image");
validate($diskrom, 256, "a DiskII ROM image");
validate($parallelrom, 256, "a parallel card ROM image");

dumpRom($romfile, "apple/applemmu-rom.h", "romData", 32768);
dumpRom($diskrom, "apple/diskii-rom.h", "romData", 256);
dumpRom($parallelrom, "apple/parallel-rom.h", "romData", 256);
exit 0;

sub validate {
    my ($filename, $expectedSize, $description) = @_;

    my @info = lstat($filename);
    
    die "Unable to stat '$filename' file: $!"
	unless scalar @info;
    
    die "This doesn't look like $description"
	unless ($info[7] == $expectedSize);
}

sub dumpRom {
    my ($input, $output, $dataname, $datasize) = @_;

    open(IN, "<", $input) || die "Unable to open $input file: $!";
    open(OUT, ">", "$output") || die "Unable to create $output: $!";
    
    
    print OUT <<EOF
#ifndef TEENSYDUINO
#define PROGMEM
#endif

static
    uint8_t romData[$datasize] PROGMEM = { 
EOF
    ;

    my $data;
    my $count = 0;
    while (sysread(IN, $data, 1)) {
        print OUT sprintf("0x%.2X, ", ord($data));
	$count++;
	if ($count % 8 == 0) {
	    print OUT "\n";
	}
    }
    print OUT "};\n";

    close OUT;
    close IN;
}
