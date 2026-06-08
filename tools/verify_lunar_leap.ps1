# Verify lunar leap-month slot stepping for every year 1901-2099.
$lines = Get-Content "src\util\LunarCalendar.cpp"
$in = $false
$vals = @()
foreach ($line in $lines) {
  if ($line -match 'kLunarYearInfo') { $in = $true; continue }
  if ($in) {
    if ($line -match '};') { break }
    $vals += [regex]::Matches($line, '0x[0-9A-Fa-f]+') | ForEach-Object { $_.Value }
  }
}

function Get-Intercalary($year) {
  $v = [Convert]::ToInt32($vals[$year - 1901], 16)
  return ($v -band 0xF00000) -shr 20
}

function To-Slot($month, $leap, $intercalary) {
  $slot = $month - 1
  if ($intercalary -gt 0) {
    if ($month -gt $intercalary) { $slot++ }
    if ($month -eq $intercalary -and $leap) { $slot++ }
  }
  return $slot
}

function From-Slot($slot, $intercalary) {
  $leap = $false
  if ($intercalary -le 0) {
    return @{ month = $slot + 1; leap = $false }
  }
  if ($slot -lt $intercalary) {
    return @{ month = $slot + 1; leap = $false }
  }
  if ($slot -eq $intercalary) {
    return @{ month = $intercalary; leap = $true }
  }
  return @{ month = $slot; leap = $false }
}

function Bump-LunarMonth([ref]$year, [ref]$month, [ref]$leap, $delta) {
  $y = $year.Value
  $m = $month.Value
  $l = $leap.Value
  $intercalary = Get-Intercalary $y
  $slotCount = if ($intercalary -gt 0) { 13 } else { 12 }
  $slot = To-Slot $m $l $intercalary
  $slot += $delta

  while ($slot -lt 0) {
    $y--
    if ($y -lt 1901) {
      $y = 1901
      $slot = 0
      break
    }
    $intercalary = Get-Intercalary $y
    $slotCount = if ($intercalary -gt 0) { 13 } else { 12 }
    $slot += $slotCount
  }
  while ($slot -ge $slotCount) {
    $y++
    if ($y -gt 2099) {
      $y = 2099
      $intercalary = Get-Intercalary $y
      $slotCount = if ($intercalary -gt 0) { 13 } else { 12 }
      $slot = $slotCount - 1
      break
    }
    $slot -= $slotCount
    $intercalary = Get-Intercalary $y
    $slotCount = if ($intercalary -gt 0) { 13 } else { 12 }
  }

  $intercalary = Get-Intercalary $y
  $parsed = From-Slot $slot $intercalary
  $year.Value = $y
  $month.Value = $parsed.month
  $leap.Value = $parsed.leap
}

$errors = @()
$leapYears = @()
for ($y = 1901; $y -le 2099; $y++) {
  $ic = Get-Intercalary $y
  if ($ic -gt 0) { $leapYears += [pscustomobject]@{ Year = $y; Month = $ic } }

  # Each year must expose exactly one extra slot when intercalary.
  $expectedSlots = if ($ic -gt 0) { 13 } else { 12 }
  $seen = @{}
  for ($slot = 0; $slot -lt $expectedSlots; $slot++) {
    $parsed = From-Slot $slot $ic
    $key = "$($parsed.month):$($parsed.leap)"
    if ($seen.ContainsKey($key)) {
      $errors += "Year $y duplicate slot mapping at slot $slot -> $key"
    }
    $seen[$key] = $true
  }
  if ($seen.Count -ne $expectedSlots) {
    $errors += "Year $y slot count mismatch: got $($seen.Count) expected $expectedSlots"
  }

  # Forward walk through entire year and back.
  $year = $y
  $month = 1
  $leap = $false
  $path = @("$year-$month$(if($leap){'L'})")
  for ($i = 0; $i -lt $expectedSlots - 1; $i++) {
    Bump-LunarMonth ([ref]$year) ([ref]$month) ([ref]$leap) 1
    $path += "$year-$month$(if($leap){'L'})"
  }
  if ($year -ne $y -or $month -ne 12 -or $leap) {
    $errors += "Year $y forward walk did not end on month 12: ended $year-$month leap=$leap path=$($path -join ' -> ')"
  }
  for ($i = 0; $i -lt $expectedSlots - 1; $i++) {
    Bump-LunarMonth ([ref]$year) ([ref]$month) ([ref]$leap) -1
  }
  if ($year -ne $y -or $month -ne 1 -or $leap) {
    $errors += "Year $y backward walk did not return to month 1: ended $year-$month leap=$leap"
  }

  # Intercalary year must visit leap month exactly once when stepping from month 1 through year.
  if ($ic -gt 0) {
    $year = $y; $month = 1; $leap = $false
    $leapVisits = 0
    for ($slot = 0; $slot -lt $expectedSlots; $slot++) {
      if ($leap -and $month -eq $ic) { $leapVisits++ }
      Bump-LunarMonth ([ref]$year) ([ref]$month) ([ref]$leap) 1
    }
    if ($leapVisits -ne 1) {
      $errors += "Year $y intercalary month $ic visited $leapVisits times (expected 1)"
    }
  }
}

Write-Host "Leap years in range: $($leapYears.Count)"
$leapYears | Select-Object -First 5 | Format-Table
Write-Host "... 1993-1997:"
$leapYears | Where-Object { $_.Year -ge 1993 -and $_.Year -le 1997 } | Format-Table

if ($errors.Count -eq 0) {
  Write-Host "OK: all leap-year slot walks passed ($($leapYears.Count) leap years)."
  exit 0
} else {
  Write-Host "FAILED with $($errors.Count) error(s):"
  $errors | ForEach-Object { Write-Host $_ }
  exit 1
}
