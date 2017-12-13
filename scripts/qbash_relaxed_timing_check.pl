#! /usr/bin/perl - w

# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT license.


# Run a file of test queries in batch mode against thea big test index 
# with a few different option combinations and report response times.

# Assumes run in a directory with the following subdirectories:

$idxdir = "../test_data";
$tqdir = "../test_queries";

$ix = "$idxdir/Wikipedia_titles";
@qsets = ("$tqdir/emulated_log_10k.q",
	  "$tqdir/emulated_log_four_full_words_10k.q",
	  "$tqdir/emulated_log_four_words_with_operators.q",
);


$tfile = "/tmp/relaxed_timing.out";
$|++;

die "Usage: $0 <QBASHQ_binary> 
   Note: This script expects a current index in $ix\n" 
	unless ($#ARGV >= 0);

$qp = $ARGV[0];
$qp = "../src/visual_studio/x64/Release/QBASHQ.exe"
    if $qp eq "default";

die "$qp is not executable\n" unless -e $qp;
$fail_fast = 0;
$fail_fast = 1 if ($#ARGV > 0 && $ARGV[1] eq "-fail_fast");

die "Can't find QBASHER indexes in $ix\n" 
	unless (-r "$ix/0/QBASH.if") || (-r "$ix/QBASH.if");

$errs = 0;

@options = (
    "-relaxation_level=0",
    "-relaxation_level=1",
    "-relaxation_level=2",
    "-relaxation_level=3",
    );


print "Query processing timings against $ix:

Options              Ave_resp_msec   95%_resp   99%_resp        QPS
===================================================================
";


for $qset (@subq_sets) {
    print "  --- $qset ---\n";
    $cmd = "$qp index_dir=$ix < $qset > $tfile";
    $code = system($cmd);

    die "Batch run ($cmd) killed by signal\n" if ($code & 255);
    $code >>= 8;
    die "Batch run ($cmd) crashed with code $code\n" if $code;

    $errs += analyse_log("Sub-query-excludes");
    unlink $tfile;
}


for $qset (@qsets) {
    print "  --- $qset ---\n";
    for $opts (@options) {
	#print "Running test set with options '$opts'\n";
	$cmd = "$qp -warm_indexes=TRUE index_dir=$ix $opts <$qset > $tfile";
	$code = system($cmd);

	#print "Output of batch run in: $tfile\n\n";
	die "Batch run ($cmd) killed by signal\n" if ($code & 255);
	$code >>= 8;
	die "Batch run ($cmd) crashed with code $code\n" if $code;

	#print "Successfully completed $qset for options: $opts.\n";
	$errs += analyse_log($opts);
	unlink $tfile;
    }
}


print "\n\n\"Brilliant!!\"\n" unless $errs;
exit($errs);

# -----------------------------------------------------------
sub analyse_log {
    my $opts = shift;
	die "Can't open $tfile\n" unless open T, $tfile;
	$initial = 1;
	undef %perc;
	undef $ave_msec;
	undef $max_msec;
	$qcnt = 0;
	while(<T>) {
		#Pagefile usage: current 5287936, peak 5296128  -- 5.0MB
		if (/Pagefile usage: current ([0-9]+), peak ([0-9]+)/) {
			if ($initial) {
				$initial = 0;
				$initialpfu = $1;
				$peakpfu = $2;
			} else {
				$currentpfu = $1;
				$peakpfu = $2;
			}
		} elsif (/^Average elapsed msec per query: ([0-9]+)/) {
			$ave_msec = $1;
			#print $_;
		} elsif (/^Maximum elapsed msec per query: ([0-9]+) +\(.*\)/) {
			$max_msec = $1;
			#print $_, "Elapsed time percentiles:\n";
		} elsif (/^ +([0-9.]+)th - +([0-9]+)/) {
			#print $_;
			$perc{$1} = $2;
		} elsif (/    Elapsed time [0-9]+ msec/) {
			$qcnt++;
			#print "  --- $qcnt ---\n" if $qcnt %100000 == 0;
		} elsif (/Total elapsed time: .*? -- ([0-9.]+) QPS/) {
		    $QPS = $1;
			#print "  --- $qcnt ---\n" if $qcnt %100000 == 0;
		}
	}

    $p95 = $perc{"95"};
    $p99 = $perc{"99"};
    print "$opts\t$ave_msec\t$p95\t$p99\t$QPS\n";

	$grewby = $currentpfu - $initialpfu;
	$MB = sprintf("%.1f", $grewby /1024/1024);

	$err_cnt = 0;
	if ($MB > 2 && $currentpfu > 1.5 * $initialpfu) {
	    if ($MB < 50) {
		print "Warning: Memory (PFU) grew by ${MB}MB.\n";
	    } else {
		print "ERROR: Memory grew too much (by ${MB}MB), probable leak.\n";
		$err_cnt++;
		exit(1) if $fail_fast;
	    }
	}

	return $err_cnt;
}
