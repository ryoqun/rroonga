# Copyright (C) 2009  Kouhei Sutou <kou@clear-code.com>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License version 2.1 as published by the Free Software Foundation.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

class VersionTest < Test::Unit::TestCase
  def test_build_version
    assert_match(/\A\d+\.\d+\.\d+\z/, Groonga.build_version)
    assert_match(Groonga.build_version, Groonga::BUILD_VERSION.join("."))
  end

  def test_version
    assert_match(/\A\d+\.\d+\.\d+(?:\.\d+-[a-zA-Z\d]+)?\z/, Groonga.version)
    assert_match(Groonga.version, Groonga::VERSION.join("."))
  end

  def test_bindings_version
    assert_match(/\A\d+\.\d+\.\d+\z/, Groonga.bindings_version)
    assert_match(Groonga.bindings_version, Groonga::BINDINGS_VERSION.join("."))
  end
end
