#!/usr/bin/perl -w

# This script automatically builds the LNG file from specially formatted C++ header file.
# Expected format is:
#	// Some comment
#	#define LNG_IDENTIFIER1   123       // "Original string"
#	#define LNG_IDENTIFIER2   125       // "Another string"
#
# Resultant LNG file is:
#	; Some comment
#	123="Original string"
#	125="Another string"

use strict;
use feature 'unicode_strings';

if (scalar(@ARGV) < 2) {
	die "Usage:\n$0 LanguageIDs.h eng.lng\n";
}

# Expect input file to be in Win-1251 encoding; output LNG file will be UTF-16LE.
open(TEMPLATE, '<:encoding(cp-1251)', $ARGV[0]) or die "Failed to open file '$ARGV[0]' for reading: $!";
open(LANGFILE, '>:raw:encoding(utf-16le):crlf', $ARGV[1]) or die "Failed to open file '$ARGV[1]' for writing: $!";

# Print LNG file header.
print LANGFILE "\N{U+FEFF}; English translation for Uninstaller64 1.0.0\n";
print LANGFILE "; Author: Konstantin Vlasov (support\@flint-inc.ru)\n";

# Start processing the file.
while (my $line = <TEMPLATE>) {
	if ($line =~ m/^$/) {
		# Keep empty lines.
		print LANGFILE $line;
	}
	elsif ($line =~ m/^\s*\x23define\s*\S+\s+(\d+)\s*\/\/\s*(".*")$/) {
		# Transform constant definitions that contain comments with text for language file.
		print LANGFILE "$1=$2\n";
	}
	elsif ($line =~ m/^\/\/\s*(.*)$/) {
		# Transform comments from C++ style into semicolon.
		print LANGFILE "; $1\n";
	}
	# Everything else is ignored and skipped.
}

close(TEMPLATE);
close(LANGFILE);
