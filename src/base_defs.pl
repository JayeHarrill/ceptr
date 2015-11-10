#!/usr/local/bin/perl

# This file generates c code that defines system semantic definitions.
# it reads the file "base_defs" as a source file for creating the definitions
#
# Copyright (C) 2013-2015, The MetaCurrency Project (Eric Harris-Braun, Arthur Brock, et. al).  This file is part of the Ceptr platform and is released under the terms of the license contained in the file LICENSE (GPLv3).

use strict;
use warnings;
use Data::Dumper;

my $fh = openf('<','src/base_defs');
my $cfh = openf('>','src/base_defs.c');
my $hfh = openf('>','src/base_defs.h');

sub openf {
    my $rw = shift;
    my $fn = shift;
    open(my $fh,"$rw:encoding(UTF-8)", $fn)
            or die "Could not open '$fn' $!";
    return $fh
}

my %c;
my @d;
my %fmap = ('Structure'=>'sT','StructureS'=>'sTs','Symbol'=>'sY','Process'=>'sP','SetSymbol'=>'sYs');
my $context = "SYS";
my %declared;
my %anon;
my %comments;

sub addDef {
    my $type = shift;
    my $context = shift;
    my $name = shift;
    my $def = shift;
    my $comment = shift;
    my $def_type = ($type eq 'Structure' && $def =~ /sT_/) ? "StructureS" : $type;
    push @d,[$def_type,$context.'_CONTEXT',$name,$def];
    $comments{$name} = $comment;

    # don't need to redo header defs stuff for just setting symbols definition
    if ($type ne 'SetSymbol') {
        if (! exists $c{$context}) {
            $c{$context} = {};
        }

        my $defs = $c{$context};

        if (! exists $defs->{$type}) {
            $defs->{$type} = [];
        }

        my $a = $defs->{$type};
        push @$a, $name;
    }
}

sub andify {
    my $n = shift;
    join("_AND_",split(/,/,$n))
}
sub makeName {
    my $n = shift;
    $n =~ s/sT_SYM\(([^)]+)\)/$1/g;
    $n =~ s/sT_SET\([0-9]+,/ONE_OF_/g;
    $n =~ s/sT_SEQ\(2,([^,]+),\g1/PAIR_OF_$1/g;
    $n =~ s/sT_SEQ\(2,([^)]+)\)/TUPLE_OF_$1/g;
    $n =~ s/sT_SEQ\([0-9]+,/LIST_OF_/g;
    $n =~ s/sT_OR/LOGICAL_OR_OF_/g;
    $n =~ s/sT_STAR/ZERO_OR_MORE_OF_/g;
    $n =~ s/sT_PLUS/ONE_OR_MORE_OF_/g;
    $n =~ s/sT_QMRK/ZERO_OR_ONE_OF_/g;
    $n =~ s/[()]//g;
    return &andify($n);
}

sub buildNumParams {
    my $x = shift;
    my @params = split /,/,$x;
    return scalar(@params).";".join(';',@params);
}

sub convertStrucDef {
#    my @tokens = split /([,\(\)\{\}\|\?\+\*])/,shift;
    my $x= shift;
    #for a simple structure without optionality we'll just use sT with params
    if (!($x=~/[\{\}\(\)\+\*?|]/)) {
        $x = &buildNumParams($x);
    }
    else {
        $x =~ s/([a-zA-Z0-9_]+)/sT_SYM<$1>/g;
        while ($x=~/\|/) {
            $x =~ s/\|\[([^|\]]+)\|([^\]]+)\]/sT_OR<$1;$2>/;
        }
        while ($x=~/[,()\{\}?+*]/) {
            $x =~ s/\*([a-zA-Z0-9_<>;]+)/sT_STAR<$1>/g;
            $x =~ s/\+([a-zA-Z0-9_<>;]+)/sT_PLUS<$1>/g;
            $x =~ s/\?([a-zA-Z0-9_<>;]+)/sT_QMRK<$1>/g;
            $x =~ s/\(([^()]+)\)/"sT_SEQ<".&buildNumParams($1).'>'/eg;
            $x =~ s/\{([^\}\{]+)\}/"sT_SET<".&buildNumParams($1).'>'/eg;
        }
        $x =~ s/</(/g;
        $x =~ s/>/)/g;
    }
    $x =~ s/;/,/g;
    return $x;
}

while (my $def = <$fh>) {
    chomp $def;
    next if ($def =~ /^ *#/);       # ignore comments
    next if ($def) =~ /^[ \t]*$/;   #ignore whitespace lines
    if ($def =~ /(.*): *(.*?);(.*)/) {
        my $type = $1;
        if ($type eq 'Context') {$context = $2;next;}
        my $params = $2;
        my $comment = $3;

        if ($type eq 'Declare') {
            my @symbols = split /,/,$params;
            foreach my $s (@symbols) {
                $declared{$s} = 1;
                &addDef("Symbol",$context,$s,"NULL_STRUCTURE");
            }
        }
        else {
            if ($type eq 'Symbol') {
                $params =~ /(.*?),(.*)/;
                my $name = $1;
                my $structure = $2;
                if ($declared{$name}) {
                    $type = "SetSymbol";
                }
                if ($structure =~ /^\[(.*)\]$/) {
                    my $sdef = &convertStrucDef($1);
                    my $sname = &makeName($sdef);
                    if (!$anon{$sname}) {
                        $anon{$sname} = 1;
                        &addDef("Structure",$context,$sname,$sdef);
                    }
                    $structure = $sname;
                }
                &addDef($type,$context,$name,$structure,$comment);
            }
            elsif ($type eq 'Structure') {
                $params =~ /(.*?),(.*)/;
                my $name = $1;
                my $structure_def = $2;
                &addDef($type,$context,$name,&convertStrucDef($structure_def),$comment);
            }
            elsif ($type eq 'Process') {
                $params =~ /(.*?),(.*)/;
                my $name = $1;
                my $process_def = $2;
                &addDef($type,$context,$name,$process_def,$comment);
            }
        }

    } else {
        die "unable to parse $def";
    }
}
#print Dumper(\@d);
#print $fmap{'Symbol'};

print $cfh <<'EOF';
/**
 * @ingroup def
 *
 * @file base_defs.c
 * @brief auto-generated system definitions
 *
 * NOTE: this file is auto-generated by base_defs.pl
 *
 * @copyright Copyright (C) 2013-2015, The MetaCurrency Project (Eric Harris-Braun, Arthur Brock, et. al).  This file is part of the Ceptr platform and is released under the terms of the license contained in the file LICENSE (GPLv3).
 */

#include "base_defs.h"
#include "sys_defs.h"
#include "def.h"
#include "process.h"

void base_defs() {
EOF
foreach my $s (@d) {
    my @x = @$s;
    my $n = shift @x;
    my $p = join(',',@x);
    print $cfh "  $fmap{$n}($p);\n";
}

print $cfh <<EOF;
}
EOF


print $hfh <<'EOF';
/**
 * @ingroup def
 *
 * @file base_defs.h
 * @brief auto-generated system definitions
 *
 * NOTE: this file is auto-generated by base_defs.pl
 *
 * @copyright Copyright (C) 2013-2015, The MetaCurrency Project (Eric Harris-Braun, Arthur Brock, et. al).  This file is part of the Ceptr platform and is released under the terms of the license contained in the file LICENSE (GPLv3).
 */

#ifndef _CEPTR_BASE_DEFS_H
#define _CEPTR_BASE_DEFS_H
#include "sys_defs.h"

void base_defs();
EOF

&hout("SYS","Symbol");
&hout("SYS","Structure");
&hout("SYS","Process");
&hout("TEST","Symbol");
&hout("LOCAL","Symbol");
&hout("LOCAL","Structure");

# add definitions to the header file
sub hout {
    my $context = shift;
    my $type = shift;
    my $types = $type eq "Process" ? "Processes" : $type."s";

    my $defs = $c{$context};
    my $a = $defs->{$type};
    print $hfh <<EOF;

/**********************************************************************************/
// $context:$type
enum $context${\($type)}IDs {
    NULL_${\(($context ne 'SYS' ? $context.'_' : '').uc($type))}_ID,
EOF
    foreach my $s (@$a) {
        print $hfh '    '.$s."_ID,\n";

    }
    print $hfh '    NUM_'.$context.'_'.uc($types)."\n};\n";
    foreach my $s (@$a) {
        print $hfh '#define '.$s." G_contexts[$context"."_CONTEXT].".lc($types).'['.$s."_ID]\n";
    }
}
print $hfh <<EOF;

#endif
EOF

#generate sys process documentation file
my $pdfh = openf('>','doxy/sys_processes.html');
my $phtml = << 'HTML';
<table class="doxtable"><tr><th>Process</th><th>Inputs</th><th>Output</th><th>Comments</th></tr>
HTML
my $stdfh = openf('>','doxy/sys_structures.html');
my $sthtml = << 'HTML';
<table class="doxtable"><tr><th>Structures</th><th>Symbols</th></th><th>Comments</th></tr>
HTML
my $sydfh = openf('>','doxy/sys_symbols.html');
my $syhtml = << 'HTML';
<table class="doxtable"><tr><th>Symbol</th><th>Structure</th><th>Comments</th></tr>
HTML

foreach my $s (@d) {
    my @x = @$s;
    my $type = $x[0];
    my $context = $x[1];
    my $name = $x[2];
    my $def = $x[3];
    if ($type eq 'Process') {
        my ($desc,$out,$out_type,$out_sym,@def) = split /,/,$def;
        $desc =~ /"(.*)"/;
        $desc = $1;
        $out = &processSig($out,$out_type,$out_sym);
        my $in = "";
        while ($def[0] && $def[0] ne '0') {
            my $n = shift @def;
            my $optional = 0;
            if ($def[0] eq 'SIGNATURE_OPTIONAL') {
                $optional = 1;
                shift @def;
            }
            my $i = &processSig($n,shift @def,shift @def);
            $i = "[$i]" if $optional;
            $in .= "<li>$i</li>";
        }
        my $c = "<i>$desc</i>";
        $c = "$c<br />".$comments{$name} if $comments{$name};
        $phtml .= "<tr><td><a name=\"$name\"></a>$name</td><td>$in</td><td>$out</td><td>$c</td></tr>\n";
    }
    elsif ($type eq 'Symbol') {
        $def =~ s/_/-/g;
        $def = "<a href=\"ref_sys_structures.html#$def\">$def</a>";
        my $c = $comments{$name} ? $comments{$name} : "";
        $syhtml .= "<tr><td><a name=\"$name\"></a>$name</td><td>$def</td><td>$c</td></tr>\n";
    }
    elsif ($type eq 'Structure') {
        my ($count,@def) = split /,/,$def;

        for(@def) {
            s/(.*)/<a href="ref_sys_symbols.html#$1">$1<\/a>/;
        }

        $def = join(', ',@def);
        $def = "SEQ($def)" if (scalar @def > 1);
        $sthtml .= "";
        my $c = $comments{$name} ? $comments{$name} : "";
        $sthtml .= "<tr><td><a name=\"$name\"></a>$name</td><td>$def</td><td>$c</td></tr>\n";
    }
    elsif ($type eq 'StructureS') {
        my $n = $name;
        $n =~ s/_/-/g;
        $def =~ s/sT_//g;
        $def =~ s/,/, /g;
        $def =~ s/SYM\((.*?)\)/<a href="ref_sys_symbols.html#$1">$1<\/a>/g;
        $def =~ s/(SEQ|SET)\([0-9]+, /$1(/g;
        $def =~ s/STAR/\*/g;
        $def =~ s/PLUS/\+/g;
        $def =~ s/QMRK/\?/g;
        my $c = $comments{$name} ? $comments{$name} : "";
        $sthtml .= "<tr><td><a name=\"$name\"></a>$n</td><td>$def</td><td>$c</td></tr>\n";
    }

}

&finish($phtml,$pdfh);
&finish($sthtml,$stdfh);
&finish($syhtml,$sydfh);

sub finish {
    my $html = shift;
    my $fh = shift;
    $html .= << 'HTML';
</table>
HTML
    print $fh $html;
}

sub processSig {
    my $name = shift;
    my $type = shift;
    my $sym = shift;
    $name =~ /"(.*)"/;$name = $1;
    $type =~ /SIGNATURE_(.*)/;$type = $1;
    $sym =~ s/(.*)/<a href="ref_sys_symbols.html#$1">$1<\/a>/;

    return "$name($type:$sym)";
}