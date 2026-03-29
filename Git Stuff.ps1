cd "e:\Visual Studio Code\Git\timer_project"

git config user.name "JFrancisReyes"
git config user.email "JohnfrancisO.reyes@gmail.com"

git init

git add .

git commit -m "Initial commit - Timer project with main and subsystem"

git remote add origin https://github.com/JFrancisReyes/timer_project.git

git branch -M main

git push -u origin main