-- notify.lua 不要写 wrk.path！
wrk.method = "POST"
wrk.body = '{"command":"notify","device_type":1,"name":"agsspds","version":"6.0.0.1"}'
wrk.headers = {
  ["Content-Type"] = "application/json"
}
