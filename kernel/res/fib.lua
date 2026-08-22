function fib(n)
  if n <= 2 then
    return 1
  end
  return fib(n-1)+fib(n-2)
end
print("ÖÐÎÄ²âÊÔ")
print(fib(tonumber(input())))
