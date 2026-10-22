-- Runs only inside the disposable portable instance created by the Python driver.
local r = reaper
local root = r.GetResourcePath():gsub('\\','/')
assert(root:find('/latency%-results/'), 'Disposable test resource path required')
local cfg = dofile(root..'/Scripts/latency-config.lua')
local function quote(s) return '"'..tostring(s):gsub('\\','\\\\'):gsub('"','\\"'):gsub('\n','\\n'):gsub('\r','\\r')..'"' end
local function json(v)
  if type(v)=='string' then return quote(v) end
  if type(v)=='number' or type(v)=='boolean' then return tostring(v) end
  if v==nil then return 'null' end
  local t={};for k,x in pairs(v) do t[#t+1]=quote(k)..':'..json(x) end
  return '{'..table.concat(t,',')..'}'
end
local out=assert(io.open(cfg.output,'w'))
local status=cfg.output..'.status'
local function progress(s) local f=assert(io.open(status,'w'));f:write(s);f:close() end
local function diag(id) return r.ReaWeb_GetDiagnostics(id) or '{}' end
local function number(d,key) return tonumber(d:match('"'..key..'":([%d%.]+)')) end
local function wait(seconds) local t=r.time_precise()+seconds;repeat coroutine.yield() until r.time_precise()>=t end
local ids,maxid={},0
local function write(row,d)
  out:write(json(row):sub(1,-2)..',"diagnostics":'..(d or '{}')..'}\n');out:flush()
end
local track,take,fxs
local function show(fx,mode)
  if take then r.TakeFX_Show(take,fx,mode) else r.TrackFX_Show(track,fx,mode) end
end
local function guid(fx) return take and r.TakeFX_GetFXGUID(take,fx) or r.TrackFX_GetFXGUID(track,fx) end
local function identify(fx)
  local g=guid(fx)
  local bytes={};for h in g:gsub('[{}%-]',''):gmatch('%x%x') do bytes[#bytes+1]=tonumber(h,16) end
  local native={};for _,i in ipairs({4,3,2,1,6,5,8,7,9,10,11,12,13,14,15,16}) do native[#native+1]=bytes[i] end
  local expected='"fxGuid":['..table.concat(native,',')..']'
  for id=1,maxid+3 do
    if r.ReaWeb_IsOpen(id) then
      local d=diag(id)
      if d:find(expected,1,true) then maxid=math.max(id,maxid); return id,d end
    end
  end
end
local function setup(case)
  if track then r.DeleteTrack(track);track=nil;take=nil;wait(.25) end
  r.InsertTrackAtIndex(0,false);track=r.GetTrack(0,0);r.SetOnlyTrackSelected(track)
  r.GetSetMediaTrackInfo_String(track,'P_NAME','Latency benchmark',true)
  if case.owner=='take' then
    local item=r.CreateNewMIDIItemInProj(track,0,1,false);take=r.GetActiveTake(item)
  end
  fxs={}
  for i=1,(case.instances or 1) do
    local name='Latency/'..case.page..'/1175 Compressor.jsfx'
    local fx=take and r.TakeFX_AddByName(take,name,1) or r.TrackFX_AddByName(track,name,false,-1)
    assert(fx>=0,'Cannot insert '..name);fxs[i]=fx
  end
  if case.owner=='container' then
    assert(#fxs==1)
    local outer=r.TrackFX_AddByName(track,'Container',false,-1)
    assert(outer>=0,'Container unavailable')
    local dest=0x2000000+(outer+1)+(r.TrackFX_GetCount(track)+1)
    r.TrackFX_CopyToTrack(track,fxs[1],track,dest,true)
    local outer2=r.TrackFX_AddByName(track,'Container',false,-1)
    r.TrackFX_CopyToTrack(track,0,track,0x2000000+(outer2+1)+(r.TrackFX_GetCount(track)+1),true)
    local ok,inner=r.TrackFX_GetNamedConfigParm(track,0,'container_item.0')
    assert(ok,'Inner container unavailable')
    local found,child=r.TrackFX_GetNamedConfigParm(track,tonumber(inner),'container_item.0')
    assert(found,'Nested container child unavailable');fxs[1]=tonumber(child)
  end
end
local co=coroutine.create(function()
  assert(r.APIExists('ReaWeb_GetDiagnostics'))
  progress('started '..r.GetAppVersion())
  if cfg.settle and cfg.settle>0 then wait(cfg.settle) end
  local keeper
  if cfg.resident then
    keeper=r.ReaWeb_OpenEx(root..'/Scripts/starter/index.html','{"hidden":true}')
    assert(keeper>0,r.ReaWeb_GetLastError());maxid=keeper
    local untilTime=r.time_precise()+20
    repeat coroutine.yield() until r.ReaWeb_IsReady(keeper) or r.time_precise()>untilTime
    assert(r.ReaWeb_IsReady(keeper),'Keeper not ready');wait(1)
  end
  for ci,case in ipairs(cfg.cases) do
    local isjs=case.scenario:sub(1,1)=='J'
    if isjs then setup(case) end
    local retained
    if not case.cold then wait(1) end
    for sample=1,(case.samples or 33) do
      local fx=fxs and fxs[((sample-1)%#fxs)+1]
      local row={traceId=cfg.run..':'..ci..':'..sample,run=cfg.run,scenario=case.scenario,page=case.page,
        owner=case.owner or 'track',mode=case.mode or 'floating',instances=case.instances or 1,
        sample=sample,warmup=not case.cold and sample<=3,idleTarget=cfg.idle,coldProcess=case.cold or false,resident=cfg.resident or false,settleSeconds=cfg.settle or 0,
        actualVisible='unmeasured',trustedInteractive='unmeasured',thread='REAPER main via defer'}
      progress(row.traceId..' '..case.scenario..' '..case.page)
      local oldActivation,id,d
      if isjs then
        id,d=identify(fx);oldActivation=d and number(d,'activation')
      end
      local start=r.time_precise();row.requestClockSeconds=start
      if isjs then show(fx,case.mode=='chain' and 1 or 3);row.apiReturnMs=(r.time_precise()-start)*1000
      elseif case.page=='demo' then
        dofile(root..'/Scripts/demo/ReaWebAPI_Demo.lua');id=maxid+1;maxid=id
        row.apiReturnMs=(r.time_precise()-start)*1000
      elseif case.scenario=='A3' and retained then
        id=r.ReaWeb_OpenEx(root..'/Scripts/'..case.page..'/index.html','{"instanceKey":"latency-retain"}')
        row.apiReturnMs=(r.time_precise()-start)*1000
      else
        local opts=case.scenario=='A4' and '{"hidden":true}' or case.scenario=='A3' and '{"instanceKey":"latency-retain"}' or '{"multiple":true}'
        id=r.ReaWeb_OpenEx(root..'/Scripts/'..case.page..'/index.html',opts)
        assert(id>0,r.ReaWeb_GetLastError());maxid=math.max(id,maxid)
        row.apiReturnMs=(r.time_precise()-start)*1000
      end
      local done=false;local showStart;local checks=0;local pollCost=0;local lastPoll=start;local maxGap=0
      repeat
        coroutine.yield()
        local t=r.time_precise();maxGap=math.max(maxGap,(t-lastPoll)*1000);lastPoll=t
        if isjs then id,d=identify(fx) else d=diag(id) end
        checks=checks+1
        if d then
          local current=not isjs or (d:find('"cached":false',1,true) and (not oldActivation or number(d,'activation')~=oldActivation))
          if case.scenario=='A4' and not showStart and d:find('"frameReadyMs":',1,true) and d:find('LATENCY_PAGE',1,true) then
            row.preloadObservedMs=(r.time_precise()-start)*1000
            showStart=r.time_precise();r.ReaWeb_Show(id);row.showApiReturnMs=(r.time_precise()-showStart)*1000
          end
          if current and d:find('"revealCompleteMs":',1,true) and (case.scenario~='A4' or showStart) then
            done=true;row.revealObservedUpperMs=(r.time_precise()-(showStart or start))*1000
          end
        end
        pollCost=pollCost+(r.time_precise()-t)*1000
      until done or r.time_precise()-start>20
      row.ok=done;row.pollCalls=checks;row.pollCostMs=pollCost;row.maxPollGapMs=maxGap;row.windowId=id
      if id then
        wait(.25);d=diag(id)
        row.syntheticProbeLogged=d:find('LATENCY_PAGE',1,true)~=nil
      end
      row.liveWindowCount=0
      for n=1,maxid do if r.ReaWeb_IsOpen(n) then row.liveWindowCount=row.liveWindowCount+1 end end
      if not done then
        if isjs then row.nativeEditorPresent=(take and r.TakeFX_GetFloatingWindow(take,fx) or r.TrackFX_GetFloatingWindow(track,fx))~=nil end
        local f=io.open(cfg.output..'.failure','w');f:write('guid='..(isjs and guid(fx) or '')..'\n')
        for n=1,maxid+5 do f:write(n..' '..diag(n)..'\n') end;f:close()
      end
      write(row,d)
      if isjs then
        if case.scenario~='J5-switch' then show(fx,case.mode=='chain' and 0 or 2) end
        if case.scenario=='J2' or case.scenario=='J4' or case.scenario=='J6-new' then if id then r.ReaWeb_Close(id) end end
      elseif case.scenario=='A3' then retained=id
      else r.ReaWeb_Close(id) end
      wait(.25)
      if not done then error('Sample timed out: '..row.traceId) end
    end
    if retained then r.ReaWeb_Close(retained) end
    if isjs then for _,fx in ipairs(fxs) do local id=identify(fx);if id then r.ReaWeb_Close(id) end end end
  end
  if track then r.DeleteTrack(track) end
  if keeper then r.ReaWeb_Close(keeper) end
  progress('complete');out:close()
end)
local function loop()
  local ok,err=coroutine.resume(co)
  if not ok then progress('ERROR '..tostring(err));out:close();return end
  if coroutine.status(co)~='dead' then r.defer(loop) end
end
loop()
