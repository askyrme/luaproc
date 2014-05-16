require "luaproc"

-- create an additional worker
luaproc.setnumworkers( 2 )

-- create a new lua process
luaproc.newproc( [[
  -- create a communication channel
  luaproc.newchannel( "testchannel" )
  -- create a sender lua process
  luaproc.newproc( [=[
    -- send a message
    luaproc.send( "testchannel", "luaproc seems to be working fine" )
  ]=] )
  -- create a receiver lua process
  luaproc.newproc( [=[
    -- receive and print a message
    print( luaproc.receive( "testchannel" ))
  ]=] )
]] )

