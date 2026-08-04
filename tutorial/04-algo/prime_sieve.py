def prime_sieve(limit: int) -> None:
    print("primes up to", limit)
    count = 0
    for n in range(2, limit + 1):
        is_prime = True
        for divisor in range(2, n):
            if n % divisor == 0:
                is_prime = False
                break
        if is_prime:
            print("  prime:", n)
            count += 1
    print("total primes up to", limit, "=", count)


prime_sieve(30)
