# Before 'make install' is performed this script should be runnable with
# 'make test'. After 'make install' it should work as 'perl Geo-GDAL-Bayes.t'

#########################

# change 'tests => 1' to 'tests => last_test_to_print';

use strict;
use warnings;
use v5.10;

use Test::More tests => 2;
use Geo::GDAL;
use Hugin;
use PDL;

BEGIN { use_ok('Geo::GDAL::Bayes::Hugin') };

#########################

# Insert your test code below, the Test::More module is use()ed here so read
# its man page ( perldoc Test::More ) for help writing this test script.

my $rain = Geo::GDAL::Driver('MEM')->Create(Type => 'Byte', Width => 3, Height => 3)->Band;
my $sprinkler = Geo::GDAL::Driver('MEM')->Create(Type => 'Byte', Width => 3, Height => 3)->Band;
my $grass_wet = Geo::GDAL::Driver('MEM')->Create(Type => 'Float32', Width => 3, Height => 3)->Band;

my $setup = Geo::GDAL::Bayes::Hugin->new({
    domain => bn(),
    evidence => {
        rain => $rain, # states are 0 (no rain) and 1 (rain)
        sprinkler => $sprinkler, # states are 0 (off) and 1 (on)
    },
    output => {
        node => 'grass_wet',
        band => $grass_wet, # states are 0 (dry) and 1 (wet)
        state => 'T', # the state of whose probability is stored into the band
    }
});

#say STDERR $setup;
my $p = byte pdl [[0,1,0], [1,0,1], [0,1,0]];
$rain->Piddle($p);

$p = byte pdl [[1,0,1], [1,0,1], [0,1,0]];
$sprinkler->Piddle($p);

$setup->compute();

print STDERR $rain->Piddle;
print STDERR $sprinkler->Piddle;
print STDERR $grass_wet->Piddle;

my $aref = unpdl $grass_wet->Piddle;
for my $row (@$aref) {
    for my $i (0..$#$row) {
        $row->[$i] = sprintf("%.2f", $row->[$i])+0;
    }
}

is_deeply($aref, [[0.9, 0.8, 0.90],[0.99, 0, 0.99],[0, 0.99, 0]], "rain + sprinkler => grass wet");

sub bn {
    my $d = Hugin::Domain->new();
    
    my $subtype = 'label';
    
    my $rain = $d->new_node('chance', 'discrete');
    $rain->set_subtype($subtype);
    $rain->set_name('rain');
    $rain->set_number_of_states(2);
    $rain->set_state_labels('F', 'T');
    
    # rain                      F    T
    $rain->get_table->set_data(0.8, 0.2);
    
    my $sprinkler = $d->new_node('chance', 'discrete');
    $sprinkler->set_subtype($subtype);
    $sprinkler->set_name('sprinkler');
    $sprinkler->set_number_of_states(2);
    $sprinkler->set_state_labels('F', 'T');
    $sprinkler->add_parent($rain);
    
    # rain                            F    F     T     T                      
    # sprinkler                       F    T     F     T
    $sprinkler->get_table->set_data(0.6, 0.4, 0.99, 0.01);
    
    my $grass_wet = $d->new_node('chance', 'discrete');
    $grass_wet->set_subtype($subtype);
    $grass_wet->set_name('grass_wet');
    $grass_wet->set_number_of_states(2);
    $grass_wet->set_state_labels('F', 'T');
    $grass_wet->add_parent($rain);
    $grass_wet->add_parent($sprinkler);
    
    # sprinkler                     F  F    F    F    T    T     T     T
    # rain                          F  F    T    T    F    F     T     T         
    # grass_wet                     F  T    F    T    F    T     F     T
    $grass_wet->get_table->set_data(1, 0, 0.2, 0.8, 0.1, 0.9, 0.01, 0.99);
    
    $d->compile;
    return $d;
}
