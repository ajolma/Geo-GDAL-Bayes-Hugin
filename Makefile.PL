use 5.012000;
use ExtUtils::MakeMaker;

# change these to match your system:
my $libs = ['-L/home/ajolma/usr/lib -lgdal -L/usr/local/hugin/lib -L/usr/local/hugin/lib64 -lhugin'];
my $inc = '-I. -I/home/ajolma/usr/include -I/usr/local/hugin/include';

WriteMakefile(
    NAME              => 'Geo::GDAL::Bayes::Hugin',
    VERSION_FROM      => 'lib/Geo/GDAL/Bayes/Hugin.pm',
    PREREQ_PM         => {
        'Geo::GDAL' => 0,
        Hugin => 0
    },
    ABSTRACT_FROM     => 'lib/Geo/GDAL/Bayes/Hugin.pm',
    AUTHOR            => 'Ari Jolma <ajolma at gmail.com>',
    LICENSE           => 'perl_5',
    CC                => 'g++ -g',
    LD                => 'g++ -g',
    LIBS              => $libs,
    DEFINE            => '', # e.g., '-DHAVE_SOMETHING'
    INC               => $inc,
    OBJECT            => '$(O_FILES)', # link all the C files too
);
if  (eval {require ExtUtils::Constant; 1}) {
  # If you edit these definitions to change the constants used by this module,
  # you will need to use the generated const-c.inc and const-xs.inc
  # files to replace their "fallback" counterparts before distributing your
  # changes.
  my @names = (qw());
  ExtUtils::Constant::WriteConstants(
                                     NAME         => 'Geo::GDAL::Bayes::Hugin',
                                     NAMES        => \@names,
                                     DEFAULT_TYPE => 'IV',
                                     C_FILE       => 'const-c.inc',
                                     XS_FILE      => 'const-xs.inc',
                                  );

}
else {
  use File::Copy;
  use File::Spec;
  foreach my $file ('const-c.inc', 'const-xs.inc') {
    my $fallback = File::Spec->catfile('fallback', $file);
    copy ($fallback, $file) or die "Can't copy $fallback to $file: $!";
  }
}
