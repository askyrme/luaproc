-- load luaproc
luaproc = require "luaproc"

-- create an additional worker
luaproc.setnumworkers( 2 )

-- create a new lua process
luaproc.newproc( [[
  -- create a communication channel
  luaproc.newchannel( "achannel" )
  -- create a sender lua process
  luaproc.newproc( [=[
    -- send a message
    luaproc.send( "achannel", "hello world from luaproc" )
  ]=] )
  -- create a receiver lua process
  luaproc.newproc( [=[
    -- receive and print a message
    print( luaproc.receive( "achannel" ))
  ]=] )
]] )

