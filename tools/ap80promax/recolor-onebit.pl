#!/usr/bin/env perl

use strict;
use warnings;
use File::Basename qw(dirname);
use File::Find;
use File::Path qw(make_path);

sub usage
{
    die "Usage: $0 [--verify] SOURCE DEST FOREGROUND BACKGROUND\n";
}

my $verify = @ARGV && $ARGV[0] eq '--verify' ? shift @ARGV : 0;
@ARGV == 4 or usage();

my ($source, $dest, $foreground, $background) = @ARGV;

sub parse_rgb
{
    my ($value) = @_;
    $value =~ /\A[0-9a-fA-F]{6}\z/
        or die "Invalid RGB color: $value\n";
    return map { hex($_) } $value =~ /(..)(..)(..)/;
}

my @foreground_rgb = parse_rgb($foreground);
my @background_rgb = parse_rgb($background);

sub read_file
{
    my ($path) = @_;
    open my $fh, '<:raw', $path or die "Cannot read $path: $!\n";
    local $/;
    my $data = <$fh>;
    close $fh or die "Cannot close $path: $!\n";
    return $data;
}

sub recolor_bmp
{
    my ($path, $data) = @_;
    length($data) >= 54 && substr($data, 0, 2) eq 'BM'
        or die "Not a BMP file: $path\n";

    my $pixel_offset = unpack('V', substr($data, 10, 4));
    my $dib_size = unpack('V', substr($data, 14, 4));
    my $depth = unpack('v', substr($data, 28, 2));
    my $compression = unpack('V', substr($data, 30, 4));
    my $colors_used = unpack('V', substr($data, 46, 4));
    my $palette_offset = 14 + $dib_size;

    $depth == 1 or die "Expected a one-bit BMP: $path\n";
    $compression == 0 or die "Expected an uncompressed BMP: $path\n";
    $palette_offset <= $pixel_offset
        or die "Invalid BMP palette offset: $path\n";

    my $available_colors = int(($pixel_offset - $palette_offset) / 4);
    my $palette_colors = $colors_used || 2;
    $palette_colors = $available_colors
        if $palette_colors > $available_colors;
    $palette_colors > 0 or die "BMP has no palette: $path\n";

    for my $index (0 .. $palette_colors - 1)
    {
        my $offset = $palette_offset + $index * 4;
        my ($blue, $green, $red) =
            unpack('C3', substr($data, $offset, 3));
        my $luma = 299 * $red + 587 * $green + 114 * $blue;
        my @replacement = $luma >= 128_000
            ? @foreground_rgb : @background_rgb;

        substr($data, $offset, 3) =
            pack('C3', $replacement[2], $replacement[1], $replacement[0]);
    }

    return $data;
}

sub collect_files
{
    my ($root) = @_;
    return ('') if -f $root;

    my @files;
    find(
        {
            no_chdir => 1,
            wanted => sub {
                return unless -f $_;
                my $relative = $File::Find::name;
                $relative =~ s{\A\Q$root\E/?}{};
                push @files, $relative;
            },
        },
        $root
    );
    return sort @files;
}

-e $source or die "Source does not exist: $source\n";
my @files = collect_files($source);

for my $relative (@files)
{
    my $source_file = $relative eq '' ? $source : "$source/$relative";
    my $dest_file = $relative eq '' ? $dest : "$dest/$relative";
    my $source_data = read_file($source_file);
    my $expected = $source_file =~ /\.bmp\z/i
        ? recolor_bmp($source_file, $source_data) : $source_data;

    if ($verify)
    {
        -f $dest_file or die "Missing generated file: $dest_file\n";
        my $actual = read_file($dest_file);
        $actual eq $expected
            or die "Generated file differs from expected palette: $dest_file\n";
        next;
    }

    make_path(dirname($dest_file));
    open my $fh, '>:raw', $dest_file
        or die "Cannot write $dest_file: $!\n";
    print {$fh} $expected
        or die "Cannot write $dest_file: $!\n";
    close $fh or die "Cannot close $dest_file: $!\n";
    chmod((stat($source_file))[2] & 07777, $dest_file)
        or die "Cannot set permissions on $dest_file: $!\n";
}

print $verify ? "OneBit palette verified: $dest\n"
              : "OneBit palette written: $dest\n";
