source "https://rubygems.org/"

gem 'loggability', '~> 0.17'

group( :development ) do
	gem 'pry',                     '~> 0.13'
	gem 'rake',                    '~> 13.0'
	gem 'rake-compiler',           '~> 1.1'
	gem 'rake-deveiate',           '~> 0.22'
	gem 'rdoc-generator-sixfish',  '~> 0.2'
	gem 'rspec',                   '~> 3.9'
	gem 'rubocop',                 '~> 0.93'
	gem 'simplecov',               '~> 0.12'
	gem 'simplecov-console',       '~> 0.9'
end

