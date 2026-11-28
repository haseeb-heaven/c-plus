use strict;
use warnings;
use Time::HiRes qw(time);

my ($timeout, $out_file, $err_file, @cmd) = @ARGV;
die "runner: bad arguments\n" unless defined($err_file) && @cmd;
die "runner: bad timeout\n" unless defined($timeout) && $timeout =~ /^\d+$/ && $timeout > 0;

my $t0 = time;
my $pid = fork();
die "runner: fork failed: $!\n" unless defined $pid;
if (!$pid) {
    open(STDOUT, '>', $out_file) or die "runner: open stdout: $!\n";
    open(STDERR, '>', $err_file) or die "runner: open stderr: $!\n";
    exec { $cmd[0] } @cmd or do {
        print STDERR "runner: exec failed: $!\n";
        exit 127;
    };
}

my $timed_out = 0;
eval {
    local $SIG{ALRM} = sub { die "__TIMEOUT__\n" };
    alarm($timeout);
    waitpid($pid, 0);
    alarm(0);
};
if ($@) {
    $timed_out = 1;
    kill 'KILL', $pid;
    waitpid($pid, 0);
}

my $elapsed = time - $t0;
my $raw = $?;
my $code = $timed_out ? 124
         : (($raw & 127) ? 128 + ($raw & 127) : ($raw >> 8));
printf "%.6f %d\n", $elapsed, $code;
