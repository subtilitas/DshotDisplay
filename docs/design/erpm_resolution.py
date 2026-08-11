# Bidir DShot eRPM frame: 3-bit exponent, 9-bit mantissa, encodes PERIOD in us.
# eRPM = 60e6 / period. ESCs normalise the mantissa (that is what EDT detection
# relies on), so mantissa is in [256,511] and the period step is 2^exp.
def bidir_step(erpm):
    if erpm <= 0: return None
    p = 60e6 / erpm                 # period us
    e = 0
    while p / (2**e) >= 512: e += 1
    m = int(p / (2**e))
    step_p = 2**e                   # period quantisation, us
    # eRPM step = |d(eRPM)/dP| * step_p = 60e6/P^2 * step_p
    return 60e6 / (p*p) * step_p, m

print(f"{'RPM':>7} {'poles':>5} {'eRPM':>8} {'bidir dRPM_e':>13} {'KISS dRPM_e':>12} {'winner':>8}  {'bidir mech':>10} {'KISS mech':>9}")
print("-"*95)
for poles in (12,14,16):
    for rpm in (1000,3000,5000,8000,12000,20000,30000):
        erpm = rpm * poles/2
        s,m = bidir_step(erpm)
        div = poles/2
        win = "KISS" if 100 < s else "bidir"
        print(f"{rpm:>7} {poles:>5} {erpm:>8.0f} {s:>13.1f} {100:>12} {win:>8}  {s/div:>10.1f} {100/div:>9.1f}")
    print()

# crossover: where bidir step == 100 eRPM
print("crossover (bidir step == 100 eRPM):")
for poles in (12,14,16):
    lo,hi=100,60000
    for _ in range(200):
        mid=(lo+hi)/2
        s,_m=bidir_step(mid*poles/2)
        if s < 100: lo=mid
        else: hi=mid
    print(f"  {poles} poles: ~{lo:.0f} mechanical RPM")
