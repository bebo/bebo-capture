"C:\Program Files (x86)\MSBuild\14.0\bin\MSBuild.exe" /property:Configuration=Release /property:Platform=x64 /target:clean,build

mkdir dist
copy x64\Release\gst-to-dshow.DLL dist
copy x64\Release\libgstdshowfiltersink.dll dist

set FILENAME=bebo-gst-to-dshow_%TAG%.zip
"C:\Program Files\7-Zip\7z.exe" a -r %FILENAME% -w .\dist\* -mem=AES256

"C:\Program Files\Amazon\AWSCLI\aws.exe" s3api put-object --bucket bebo-app --key repo/bebo-capture/%FILENAME% --body %FILENAME%
