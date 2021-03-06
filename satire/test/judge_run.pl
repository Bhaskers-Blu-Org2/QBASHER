#! /usr/bin/perl -w

# First load the judgments into %hj.  hj is indexed by query number
# and contains semicolon-separated "<document> TAB <score>" pairs

die "Usage: $0 <judgment_file> <results_file> [<judging_depth>]\n"
    unless $#ARGV >= 1;

$jfile = $ARGV[0];
$rsltsfile = $ARGV[1];
$depth = 1000;   # Maximum rank used in calculating NDCG score.
$depth = $ARGV[2] if ($#ARGV >= 2);

$test_qid = 9999999;   # Set to problem query id, for extra debug output

print "Reading judgments from $jfile and results from $rsltsfile ....\n\n";

die "Can't load judgments file $jfile"
    unless open J, $jfile;

%gain_mapping = (
    0 => 0,
    1 => 3,
    2 => 7,
    3 => 15,
    4 => 31);

while (<J>) {
    chomp;
    if (/^([0-9]+)\s+([0-9]+)\s+([0-9])/) {
	$qnum = $1;
	$docid = $2;
	$rating = $3;
	$gain = $gain_mapping{$rating};   # Increasing the differentiation between labels
	$gain = $rating;
	$judgment = "$docid\t$gain";
	#print "RAT: $judgment.  WAS $rating\n";
	if (defined($hj{$qnum})) {
	    $hj{$qnum} .= ";$judgment";
	} else {
	    $hj{$qnum} = "$judgment";
	}
    } else {
	die "format error: '$_'\n";
    }
}

close(J);

calculate_max_dcg_scores_at_this_depth();

# Now read the results (In either TREC submission format or Bhaskar's format)

die "Can't open $rsltsfile\n"
    unless open R, $rsltsfile;
$current_qid = -1;
$ave_ndcg = 0;
$ave_p = 0;

print 
"Query   P\@$depth    NDCG\@$depth   #Unjudged
=====   ======  ======    =========\n";

while (<R>) {
    chomp;
    s /^\s+//;  # remove leading spaces
    @f = split /\s+/;
    if ($#f == 5) {
	# Assume TREC submission format
	$qid = $f[0];
	$did = $f[2];
	$rank = $f[3];
    } elsif ($#f == 3) {
	# Assume Bhaskar's submission format
	$qid = $f[0];
	$did = $f[1];
	$rank = $f[2];
    } elsif ($#f == 0) {
	next;
    } else {
	$fields = $#f + 1;
	die "Error: Non-empty lines in $rsltsfile must have either 6 fields (TREC) or 4 (Bhaskar)
Offending line was $_
It's field count was $fields\n";
    }
	    
    next if $rank > $depth;

    if ($qid != $current_qid) {
	if ($current_qid >= 0) {
	    # Report results for old query
	    print "$current_qid\t", sprintf("%6.4f\t",$relfound/$depth), sprintf("%6.4f", $cdg / $max_dcg{$current_qid}), "\t  $unjudged\n";
	    print "CDG $cdg, MAX_DCG $max_dcg{$current_qid}\n"
		if $current_qid == $test_qid;
	    if ($relfound > 0) {
		$successes++;
	    } else {
		$failures++;
	    }
	    $ave_ndcg += $cdg / $max_dcg{$current_qid};
	    $ave_p += ($relfound / $depth);
	}
	$cdg = 0.0;
	$relfound = 0;
	$unjudged = 0;
	$current_qid = $qid;
    }
#    print "Calling lookup_gain($qid, $did)\n";
    $gain = lookup_gain($qid, $did);
    if ($gain < 0) {
	$unjudged++;
	$gain = 0;
    }
    $discount = log2($rank + 1);
    $dg = $gain / $discount;
    $cdg += $dg;
    print "RANK $rank, GAIN $gain, DISCOUNT $discount --> DG = $dg  [CDG $cdg]\n"
	if $gain > 0 && $current_qid == $test_qid;
    $relfound++ if ($gain > 0);    
}

if ($current_qid >= 0) {
    # Report results for old query
    print "$current_qid\t", sprintf("%6.4f\t",$relfound/$depth), sprintf("%6.4f", $cdg / $max_dcg{$current_qid}), "\t  $unjudged\n";
    if ($relfound > 0) {
	$successes++;
    } else {
	$failures++;
    }
    $ave_ndcg += $cdg / $max_dcg{$current_qid};
    $ave_p += ($relfound / $depth);
}

$ave_ndcg /= ($successes + $failures);
$ave_p /= ($successes + $failures);

print "

Successes: $successes
Failures: $failures

Average NDCG\@$depth: ", sprintf("%6.4f", $ave_ndcg), "
Average P\@$depth: $ave_p;
    ";

exit(0);

# ---------------------------------------------------------

sub log2 {
    my $arg = shift;
    return log($arg)/log(2);
}

sub lookup_gain {
    my $qid = shift;
    my $did = shift;
    die "lookup_gain:  arg undefined\n"
	unless defined($qid) && defined($did);
    die "No hash entry for $qid\n"
	unless defined($hj{$qid});

    my @rels = split /;/, $hj{$qid};

#    if ($qid eq "2") {
#	foreach $rel (@rels) {
#	    print "Top2  $rel\n";
#	}
#    }
    
    foreach $rel (@rels) {
	if ($rel =~ /([0-9]+)\t([0-9]+)/) {
	    if ($1 eq $did) {
#		if ($qid eq "2") {
#		    print "Topic $qid:  returning $2 for $1\n";
#		}
		return $2;
	    }
	}
    }
    return -1;   # Signal unjudged.
}


sub calculate_max_dcg_scores_at_this_depth {

    my @histo;
    foreach my $qid (keys %hj) {
	undef @histo;
	my $dcg = 0;
	my @rels = split /;/, $hj{$qid};
	my $max = 0;
	foreach $rel (@rels) {
	    if ($rel =~ /([0-9]+)\t([0-9]+)/) {
		next if $2 < 1;
		$histo[$2]++;
		$max = $2 if $2 > $max;
	    }
	}

	# We now have a histogram of all the positive scores.
	my $rank = 1;
	for (my $i = $max; $i > 0; $i--) {
	    # $i is the index into the histogram, i.e the gain
	    next unless defined($histo[$i]);
	    print "HISTO[$i] = $histo[$i]\n"
		if $qid == $test_qid;
	    for (my $j = 0; $j < $histo[$i]; $j++) {
		last if $rank > $depth;
		my $dg = $i /log2($rank + 1);
		$dcg += $dg;
		print " ++ RANK $rank, GAIN $i, DG $dg, DCG $dcg\n"
		    if $qid == $test_qid;
		$rank++;
	    }
	}
	$max_dcg{$qid} = $dcg;
	print "Maximum DCG for topic $qid = $max_dcg{$qid}\n" 
	    if $qid == $test_qid;
    }
}
